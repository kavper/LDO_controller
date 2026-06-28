#include "measurements.h"

#include "adc.h"
#include "mcp3464.h"
#include "spi.h"

#include <stdbool.h>
#include <string.h>

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
  ADC_CHANNEL_0, /* ADC1_IN0_TEMP_1 */
  ADC_CHANNEL_1, /* ADC1_IN1_TEMP_2 */
  ADC_CHANNEL_6, /* ADC1_IN6_TEMP_3 */
  ADC_CHANNEL_7  /* ADC1_IN7_TEMP_4 */
};

static Measurements_Data_t s_data;
static McpMeasurement_t s_mcp_measurement;
static uint8_t s_temperature_index;
static bool s_temperature_conversion_active;
static bool s_temperature_filter_valid[MEASUREMENTS_TEMPERATURE_COUNT];

static uint32_t measurements_vout_raw_to_mV(int32_t raw)
{
  /* TODO: apply the VOUT divider, front-end gain, offset and calibration. */
  (void)raw;
  return 0U;
}

static uint32_t measurements_iout_raw_to_mA(int32_t raw)
{
  /* TODO: apply shunt resistance, current-sense gain, offset and calibration. */
  (void)raw;
  return 0U;
}

static uint32_t measurements_vin_raw_to_mV(int32_t raw)
{
  /* TODO: apply the VIN divider, front-end gain, offset and calibration. */
  (void)raw;
  return 0U;
}

static uint32_t measurements_dac_readback_raw_to_mV(int32_t raw)
{
  /* TODO: apply the DAC readback path gain/divider and channel calibration. */
  (void)raw;
  return 0U;
}

static HAL_StatusTypeDef measurements_select_mcp(McpMeasurement_t measurement)
{
  switch (measurement)
  {
    /* Differential: VOUT_DIFF = CH0 - CH1. */
    case MCP_MEAS_VOUT_DIFF:
      return MCP3464_SelectDifferential(0U, 1U);

    /* Differential: IOUT_DIFF = CH4 - CH5. */
    case MCP_MEAS_IOUT_DIFF:
      return MCP3464_SelectDifferential(4U, 5U);

    /* Differential: VIN_DIFF = CH6 - CH7. */
    case MCP_MEAS_VIN_DIFF:
      return MCP3464_SelectDifferential(6U, 7U);

    /* Single-ended: DAC_CC_READBACK = CH2 - AGND. */
    case MCP_MEAS_DAC_CC_SINGLE_ENDED:
      return MCP3464_SelectSingleEnded(2U);

    /* Single-ended: DAC_CV_READBACK = CH3 - AGND. */
    case MCP_MEAS_DAC_CV_SINGLE_ENDED:
      return MCP3464_SelectSingleEnded(3U);

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
      s_data.dac_cc_readback_mV = measurements_dac_readback_raw_to_mV(raw);
      break;

    case MCP_MEAS_DAC_CV_SINGLE_ENDED:
      s_data.dac_cv_readback_raw = raw;
      s_data.dac_cv_readback_mV = measurements_dac_readback_raw_to_mV(raw);
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

  measurements_store_mcp(raw);
  s_mcp_measurement = (McpMeasurement_t)(((uint32_t)s_mcp_measurement + 1U)
                                        % (uint32_t)MCP_MEAS_COUNT);
  (void)measurements_select_mcp(s_mcp_measurement);

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
