#include "measurements.h"

#include "adc.h"
#include "app_config.h"
#include "main.h"
#include "mcp3464.h"
#include "meas_fresh.h"
#include "ntc_temp.h"
#include "spi.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MCP3464_NOMINAL_REFERENCE_UV 3000000LL
#define MCP3464_SIGNED_CODES         32768LL

typedef enum
{
  MCP_MEAS_VOUT_DIFF = 0U,
  MCP_MEAS_IOUT_DIFF,
  MCP_MEAS_VIN_DIFF,
  MCP_MEAS_DAC_CC_SINGLE_ENDED,
  MCP_MEAS_DAC_CV_SINGLE_ENDED,
  MCP_MEAS_COUNT
} McpMeasurement_t;

static const uint32_t s_temperature_channels[MEASUREMENTS_TEMPERATURE_COUNT] =
{
  ADC_CHANNEL_0, /* T1: power MOSFET */
  ADC_CHANNEL_1, /* T2: ambient */
  ADC_CHANNEL_6, /* T3: bleeder resistor */
  ADC_CHANNEL_7  /* T4: 3.3 V LDO / 15 V-to-5 V converter area */
};

static Measurements_Data_t s_data;
static McpMeasurement_t s_mcp_measurement;
static bool s_mcp_discard_next;
static uint8_t s_temperature_index;
static bool s_temperature_conversion_active;
static uint32_t s_mcp_stamp[MCP_MEAS_COUNT];
static bool s_mcp_have[MCP_MEAS_COUNT];
static uint32_t s_ntc_stamp[MEASUREMENTS_TEMPERATURE_COUNT];
static bool s_ntc_have[MEASUREMENTS_TEMPERATURE_COUNT];
static bool s_temperature_filter_valid[MEASUREMENTS_TEMPERATURE_COUNT];

static int32_t measurements_apply_calibration(int32_t raw, int32_t zero_raw,
                                              int32_t gain_ppm)
{
  int64_t numerator = ((int64_t)raw - zero_raw) * 1000000LL;

  numerator += (numerator >= 0) ? ((int64_t)gain_ppm / 2LL)
                               : -((int64_t)gain_ppm / 2LL);
  return (int32_t)(numerator / gain_ppm);
}

static uint32_t measurements_vout_raw_to_mV(int32_t raw)
{
  int32_t calibrated = measurements_apply_calibration(
      raw, MCP3464_VOUT_ZERO_RAW, MCP3464_VOUT_GAIN_PPM);
  int64_t numerator;
  int64_t denominator;

  if (calibrated <= 0)
  {
    return 0U;
  }

  numerator = (int64_t)calibrated * MCP3464_EXTERNAL_VREF_MV
            * VOUT_DIFFAMP_INPUT_OHM;
  denominator = MCP3464_SIGNED_CODES
              * VOUT_DIFFAMP_FEEDBACK_OHM;
  return (uint32_t)((numerator + (denominator / 2LL)) / denominator);
}

static uint32_t measurements_iout_raw_to_mA(int32_t raw)
{
  int32_t calibrated = measurements_apply_calibration(
      raw, MCP3464_IOUT_ZERO_RAW, MCP3464_IOUT_GAIN_PPM);
  int64_t numerator;
  int64_t denominator;

  if (calibrated < 0)
  {
    calibrated = -calibrated;
  }
  if (calibrated == 0)
  {
    return 0U;
  }

  numerator = (int64_t)calibrated * MCP3464_EXTERNAL_VREF_MV * 1000LL;
  denominator = MCP3464_SIGNED_CODES * CURRENT_SENSE_AMPLIFIER_GAIN
              * CURRENT_SENSE_SHUNT_MILLIOHM;
  return (uint32_t)((numerator + (denominator / 2LL)) / denominator);
}

static uint32_t measurements_vin_raw_to_mV(int32_t raw)
{
  int32_t calibrated = measurements_apply_calibration(
      raw, MCP3464_VIN_ZERO_RAW, MCP3464_VIN_GAIN_PPM);
  int64_t numerator;
  int64_t denominator;

  if (calibrated <= 0)
  {
    return 0U;
  }

  numerator = (int64_t)calibrated * MCP3464_EXTERNAL_VREF_MV
            * VIN_DIFFAMP_INPUT_OHM;
  denominator = MCP3464_SIGNED_CODES
              * VIN_DIFFAMP_FEEDBACK_OHM;
  return (uint32_t)((numerator + (denominator / 2LL)) / denominator);
}

static uint32_t measurements_dac_readback_raw_to_mV(int32_t raw,
                                                     int32_t zero_raw,
                                                     int32_t gain_ppm)
{
  int32_t calibrated = measurements_apply_calibration(raw, zero_raw, gain_ppm);
  int64_t numerator;

  if (calibrated <= 0)
  {
    return 0U;
  }

  numerator = (int64_t)calibrated * MCP3464_EXTERNAL_VREF_MV;
  return (uint32_t)((numerator + (MCP3464_SIGNED_CODES / 2LL))
                    / MCP3464_SIGNED_CODES);
}

static HAL_StatusTypeDef measurements_select_mcp(McpMeasurement_t measurement)
{
  switch (measurement)
  {
    /* Differential: ADC_VOUT_P (CH2) - ADC_VOUT_N (CH3). */
    case MCP_MEAS_VOUT_DIFF:
      return MCP3464_SelectDifferential(MCP3464_CHANNEL_VOUT_P,
                                        MCP3464_CHANNEL_VOUT_N);

    /* Differential: ADC_IOUT_P (CH4) - ADC_IOUT_N (CH5). */
    case MCP_MEAS_IOUT_DIFF:
      return MCP3464_SelectDifferential(MCP3464_CHANNEL_IOUT_P,
                                        MCP3464_CHANNEL_IOUT_N);

    /* Differential: ADC_VIN_P (CH6) - ADC_VIN_N (CH7). */
    case MCP_MEAS_VIN_DIFF:
      return MCP3464_SelectDifferential(MCP3464_CHANNEL_VIN_P,
                                        MCP3464_CHANNEL_VIN_N);

    /* Single-ended: ADC_DAC_CC (CH1) - AGND. */
    case MCP_MEAS_DAC_CC_SINGLE_ENDED:
      return MCP3464_SelectSingleEnded(MCP3464_CHANNEL_DAC_CC);

    /* Single-ended: ADC_DAC_CV (CH0) - AGND. */
    case MCP_MEAS_DAC_CV_SINGLE_ENDED:
      return MCP3464_SelectSingleEnded(MCP3464_CHANNEL_DAC_CV);

    default:
      return HAL_ERROR;
  }
}

static void measurements_store_mcp(int32_t raw)
{
  switch (s_mcp_measurement)
  {
    case MCP_MEAS_VOUT_DIFF:
      s_data.vout_diff_raw = raw;
      s_data.vout_mV = measurements_vout_raw_to_mV(raw);
      break;

    case MCP_MEAS_IOUT_DIFF:
      s_data.iout_diff_raw = raw;
      s_data.iout_mA = measurements_iout_raw_to_mA(raw);
      break;

    case MCP_MEAS_VIN_DIFF:
      s_data.vin_diff_raw = raw;
      s_data.vin_mV = measurements_vin_raw_to_mV(raw);
      break;

    case MCP_MEAS_DAC_CC_SINGLE_ENDED:
      s_data.dac_cc_readback_raw = raw;
      s_data.dac_cc_readback_mV = measurements_dac_readback_raw_to_mV(
          raw, MCP3464_DAC_CC_ZERO_RAW, MCP3464_DAC_CC_GAIN_PPM);
      break;

    case MCP_MEAS_DAC_CV_SINGLE_ENDED:
      s_data.dac_cv_readback_raw = raw;
      s_data.dac_cv_readback_mV = measurements_dac_readback_raw_to_mV(
          raw, MCP3464_DAC_CV_ZERO_RAW, MCP3464_DAC_CV_GAIN_PPM);
      break;

    default:
      break;
  }

  s_mcp_have[s_mcp_measurement] = true;
  s_mcp_stamp[s_mcp_measurement] = HAL_GetTick();
}

static void measurements_mcp_task(void)
{
  int32_t raw;

  if (!MCP3464_TakeDataReadyFlag())
  {
    return;
  }

  if (MCP3464_ReadConversion(&raw) != HAL_OK)
  {
    return;
  }

  /*
   * A MUX write restarts the continuous conversion. The first IRQ can still
   * expose the result latched for the previous channel, so discard one full
   * conversion before accepting data for the newly selected input.
   */
  if (s_mcp_discard_next)
  {
    s_mcp_discard_next = false;
    return;
  }

  measurements_store_mcp(raw);
  s_mcp_measurement = (McpMeasurement_t)(((uint32_t)s_mcp_measurement + 1U)
                                        % (uint32_t)MCP_MEAS_COUNT);
  if (measurements_select_mcp(s_mcp_measurement) == HAL_OK)
  {
    s_mcp_discard_next = true;
  }

  /* TODO: migrate to MCP3464R Scan mode after settling-time requirements are known. */
}

static void measurements_temperature_task(void)
{
  ADC_ChannelConfTypeDef config = {0};
  uint16_t raw;

  if (!s_temperature_conversion_active)
  {
    config.Channel = s_temperature_channels[s_temperature_index];
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    if ((HAL_ADC_ConfigChannel(&hadc1, &config) == HAL_OK)
        && (HAL_ADC_Start(&hadc1) == HAL_OK))
    {
      s_temperature_conversion_active = true;
    }
    return;
  }

  if (__HAL_ADC_GET_FLAG(&hadc1, ADC_FLAG_EOC) == 0U)
  {
    return;
  }

  raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
  (void)HAL_ADC_Stop(&hadc1);
  s_temperature_conversion_active = false;
  s_data.temperature_raw[s_temperature_index] = raw;

  {
    NtcChannel sample;

    sample.filtered = s_data.temperature_filtered[s_temperature_index];
    sample.valid = s_temperature_filter_valid[s_temperature_index];
    sample.centi_c = s_data.temperature_centi_C[s_temperature_index];
    Ntc_ChannelApply(&sample, raw, TEMPERATURE_ADC_REFERENCE_MV,
                     TEMPERATURE_DIVIDER_SUPPLY_MV,
                     (s_temperature_index <= MEASUREMENTS_TEMP_AMBIENT)
                         ? TEMPERATURE_NTC_BETA_103AT2_K
                         : TEMPERATURE_NTC_BETA_NCP18_K);
    s_data.temperature_filtered[s_temperature_index] = sample.filtered;
    s_temperature_filter_valid[s_temperature_index] = sample.valid;
    s_data.temperature_centi_C[s_temperature_index] = sample.centi_c;
  }
  s_ntc_have[s_temperature_index] = true;
  s_ntc_stamp[s_temperature_index] = HAL_GetTick();

  s_temperature_index = (uint8_t)((s_temperature_index + 1U)
                                  % MEASUREMENTS_TEMPERATURE_COUNT);
}

static void measurements_drop_mcp(McpMeasurement_t which)
{
  switch (which)
  {
    case MCP_MEAS_VOUT_DIFF:
      s_data.vout_diff_raw = 0;
      s_data.vout_mV = 0U;
      break;
    case MCP_MEAS_IOUT_DIFF:
      s_data.iout_diff_raw = 0;
      s_data.iout_mA = 0U;
      break;
    case MCP_MEAS_VIN_DIFF:
      s_data.vin_diff_raw = 0;
      s_data.vin_mV = 0U;
      break;
    case MCP_MEAS_DAC_CC_SINGLE_ENDED:
      s_data.dac_cc_readback_raw = 0;
      s_data.dac_cc_readback_mV = 0U;
      break;
    case MCP_MEAS_DAC_CV_SINGLE_ENDED:
      s_data.dac_cv_readback_raw = 0;
      s_data.dac_cv_readback_mV = 0U;
      break;
    default:
      break;
  }
}

static void measurements_age(uint32_t now_ms)
{
  uint8_t index;

  for (index = 0U; index < (uint8_t)MCP_MEAS_COUNT; ++index)
  {
    if (!Meas_StampFresh(now_ms, s_mcp_stamp[index], s_mcp_have[index],
                         MEAS_MCP_STALE_MS))
    {
      measurements_drop_mcp((McpMeasurement_t)index);
      s_mcp_have[index] = false;
    }
  }
  for (index = 0U; index < MEASUREMENTS_TEMPERATURE_COUNT; ++index)
  {
    if (!Meas_StampFresh(now_ms, s_ntc_stamp[index], s_ntc_have[index],
                         MEAS_NTC_STALE_MS))
    {
      s_data.temperature_centi_C[index] = INT32_MIN;
      s_data.temperature_raw[index] = 0U;
      s_data.temperature_filtered[index] = 0U;
      s_temperature_filter_valid[index] = false;
      s_ntc_have[index] = false;
    }
  }
}

void Measurements_Init(void)
{
  uint8_t temperature;

  memset(&s_data, 0, sizeof(s_data));
  memset(s_temperature_filter_valid, 0, sizeof(s_temperature_filter_valid));
  memset(s_mcp_stamp, 0, sizeof(s_mcp_stamp));
  memset(s_mcp_have, 0, sizeof(s_mcp_have));
  memset(s_ntc_stamp, 0, sizeof(s_ntc_stamp));
  memset(s_ntc_have, 0, sizeof(s_ntc_have));
  /* 0 would be a real 0.00 °C. Unconverted channels stay invalid. */
  for (temperature = 0U; temperature < MEASUREMENTS_TEMPERATURE_COUNT;
       ++temperature)
  {
    s_data.temperature_centi_C[temperature] = INT32_MIN;
  }
  s_temperature_index = 0U;
  s_temperature_conversion_active = false;
  s_mcp_measurement = MCP_MEAS_VOUT_DIFF;
  s_mcp_discard_next = true;

  (void)HAL_ADCEx_Calibration_Start(&hadc1);
  if (MCP3464_Init(&hspi2) == HAL_OK)
  {
    (void)measurements_select_mcp(s_mcp_measurement);
  }
}

void Measurements_Task(void)
{
  measurements_temperature_task();
  measurements_mcp_task();
  measurements_age(HAL_GetTick());
}

bool Measurements_CriticalFresh(void)
{
  return Meas_CriticalFresh(HAL_GetTick(), s_mcp_stamp, s_mcp_have,
                            s_ntc_stamp, s_ntc_have);
}

const Measurements_Data_t *Measurements_GetData(void)
{
  return &s_data;
}

int32_t Measurements_McpRawToMicrovolts(int32_t raw)
{
  int64_t numerator = (int64_t)raw * MCP3464_NOMINAL_REFERENCE_UV;

  /* Signed rounding to the nearest microvolt, nominal VREF = 3.000 V, gain = 1. */
  numerator += (numerator >= 0) ? (MCP3464_SIGNED_CODES / 2LL)
                               : -(MCP3464_SIGNED_CODES / 2LL);
  return (int32_t)(numerator / MCP3464_SIGNED_CODES);
}
