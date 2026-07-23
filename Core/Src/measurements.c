#include "measurements.h"

#include "adc.h"
#include "app_config.h"
#include "mcp3464.h"
#include "spi.h"

#include <math.h>
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
static bool s_temperature_filter_valid[MEASUREMENTS_TEMPERATURE_COUNT];

static int32_t measurements_temperature_raw_to_centi_C(uint16_t raw,
                                                        uint32_t beta_K)
{
  const float adc_full_scale = 4095.0f;
  const float nominal_temperature_K =
      (float)TEMPERATURE_NTC_NOMINAL_KELVIN_X100 / 100.0f;
  float ntc_voltage_mV;
  float resistance_ratio;
  float temperature_K;
  float temperature_C;

  if (raw == 0U)
  {
    return INT32_MIN;
  }

  ntc_voltage_mV = ((float)raw * (float)TEMPERATURE_ADC_REFERENCE_MV)
                 / adc_full_scale;
  if (ntc_voltage_mV >= (float)TEMPERATURE_DIVIDER_SUPPLY_MV)
  {
    return INT32_MIN;
  }

  /*
   * Rpullup equals the nominal NTC resistance, so Rntc/R25 simplifies to
   * Vntc / (Vdivider - Vntc).
   */
  resistance_ratio = ntc_voltage_mV
                   / ((float)TEMPERATURE_DIVIDER_SUPPLY_MV - ntc_voltage_mV);
  temperature_K = 1.0f
                / ((1.0f / nominal_temperature_K)
                   + (logf(resistance_ratio) / (float)beta_K));
  temperature_C = temperature_K - 273.15f;
  return (int32_t)((temperature_C >= 0.0f)
                 ? (temperature_C * 100.0f + 0.5f)
                 : (temperature_C * 100.0f - 0.5f));
}

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
      raw, MCP3464_VOUT_ZERO_RAW, MCP3464_COMMON_GAIN_PPM);
  int64_t numerator;
  int64_t denominator;

  if (calibrated <= 0)
  {
    return 0U;
  }

  numerator = (int64_t)calibrated * MCP3464_EXTERNAL_VREF_MV
            * VOLTAGE_SENSE_INPUT_RESISTANCE_OHM;
  denominator = MCP3464_SIGNED_CODES
              * VOLTAGE_SENSE_FEEDBACK_RESISTANCE_OHM;
  return (uint32_t)((numerator + (denominator / 2LL)) / denominator);
}

static uint32_t measurements_iout_raw_to_mA(int32_t raw)
{
  int32_t calibrated = measurements_apply_calibration(
      raw, MCP3464_IOUT_ZERO_RAW, MCP3464_COMMON_GAIN_PPM);
  int64_t numerator;
  int64_t denominator;

  if (calibrated <= 0)
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
      raw, MCP3464_VIN_ZERO_RAW, MCP3464_COMMON_GAIN_PPM);
  int64_t numerator;
  int64_t denominator;

  if (calibrated <= 0)
  {
    return 0U;
  }

  numerator = (int64_t)calibrated * MCP3464_EXTERNAL_VREF_MV
            * VOLTAGE_SENSE_INPUT_RESISTANCE_OHM;
  denominator = MCP3464_SIGNED_CODES
              * VOLTAGE_SENSE_FEEDBACK_RESISTANCE_OHM;
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
    /* Differential: ADC_VOUT_P (CH0) - ADC_VOUT_N (CH1). */
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

    /* Single-ended: ADC_DAC_CC (CH2) - AGND. */
    case MCP_MEAS_DAC_CC_SINGLE_ENDED:
      return MCP3464_SelectSingleEnded(MCP3464_CHANNEL_DAC_CC);

    /* Single-ended: ADC_DAC_CV (CH3) - AGND. */
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
  int32_t filtered;

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

  if (!s_temperature_filter_valid[s_temperature_index])
  {
    s_data.temperature_filtered[s_temperature_index] = raw;
    s_temperature_filter_valid[s_temperature_index] = true;
  }
  else
  {
    filtered = (int32_t)s_data.temperature_filtered[s_temperature_index];
    filtered += ((int32_t)raw - filtered) / 8; /* IIR alpha = 1/8. */
    s_data.temperature_filtered[s_temperature_index] = (uint16_t)filtered;
  }

  s_data.temperature_centi_C[s_temperature_index] =
      measurements_temperature_raw_to_centi_C(
          s_data.temperature_filtered[s_temperature_index],
          (s_temperature_index <= MEASUREMENTS_TEMP_AMBIENT)
              ? TEMPERATURE_NTC_BETA_103AT2_K
              : TEMPERATURE_NTC_BETA_NCP18_K);

  s_temperature_index = (uint8_t)((s_temperature_index + 1U)
                                  % MEASUREMENTS_TEMPERATURE_COUNT);
}

void Measurements_Init(void)
{
  memset(&s_data, 0, sizeof(s_data));
  memset(s_temperature_filter_valid, 0, sizeof(s_temperature_filter_valid));
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
