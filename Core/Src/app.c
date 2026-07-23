#include "app.h"

#include "adc.h"
#include "app_config.h"
#include "bleeder.h"
#include "control.h"
#include "dac8562.h"
#include "measurements.h"
#include "mcp3464.h"
#include "output_ctrl.h"
#include "spi.h"
#include "uart_protocol.h"
#include "usart.h"

#include <stdio.h>
#include <stdint.h>

static HAL_StatusTypeDef app_configure_spi(void)
{
  HAL_StatusTypeDef spi1_status;
  HAL_StatusTypeDef spi2_status;

  /* DAC8562 uses SPI mode 1; MCP3464 uses SPI mode 0. */
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
#if APP_BRINGUP_STAGE >= 3U
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32; /* 2 MHz bring-up. */
#else
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
#endif
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  spi1_status = HAL_SPI_Init(&hspi1);

  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
#if APP_BRINGUP_STAGE >= 3U
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64; /* 1 MHz bring-up. */
#else
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
#endif
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  spi2_status = HAL_SPI_Init(&hspi2);
  return ((spi1_status == HAL_OK) && (spi2_status == HAL_OK)) ? HAL_OK : HAL_ERROR;
}

#if APP_BRINGUP_STAGE != 0U

static uint32_t s_uart_tick;

#if APP_BRINGUP_STAGE >= 2U
static uint32_t s_led_tick;
#endif

#if APP_BRINGUP_STAGE == 2U
static const uint32_t s_bringup_adc_channels[4] =
{
  ADC_CHANNEL_0,
  ADC_CHANNEL_1,
  ADC_CHANNEL_6,
  ADC_CHANNEL_7
};
#endif

static void app_bringup_led_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  HAL_GPIO_WritePin(BOARD_LED_GPIO_PORT, BOARD_LED_PIN, GPIO_PIN_SET);
  gpio.Pin = BOARD_LED_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BOARD_LED_GPIO_PORT, &gpio);
}

static void app_bringup_uart_write(const char *text, uint16_t length)
{
  (void)HAL_UART_Transmit(&huart2, (const uint8_t *)text, length, 100U);
}

#if APP_BRINGUP_STAGE == 2U
static uint16_t app_bringup_adc_read(uint32_t channel)
{
  ADC_ChannelConfTypeDef config = {0};
  uint16_t raw = UINT16_MAX;

  config.Channel = channel;
  config.Rank = ADC_REGULAR_RANK_1;
  config.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
  if ((HAL_ADC_ConfigChannel(&hadc1, &config) == HAL_OK)
      && (HAL_ADC_Start(&hadc1) == HAL_OK))
  {
    if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK)
    {
      raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }
    (void)HAL_ADC_Stop(&hadc1);
  }
  return raw;
}

static void app_bringup_send_adc_report(void)
{
  uint16_t raw[4];
  GPIO_PinState pgood;
  char report[96];
  int length;
  uint8_t index;

  for (index = 0U; index < 4U; ++index)
  {
    raw[index] = app_bringup_adc_read(s_bringup_adc_channels[index]);
  }
  pgood = HAL_GPIO_ReadPin(PGOOD_5V_IN_GPIO_Port, PGOOD_5V_IN_Pin);
  length = snprintf(report, sizeof(report),
                    "STAGE2 ADC_RAW T1=%u T2=%u T3=%u T4=%u PGOOD=%u\r\n",
                    (unsigned int)raw[0], (unsigned int)raw[1],
                    (unsigned int)raw[2], (unsigned int)raw[3],
                    (unsigned int)(pgood == PGOOD_ASSERTED_LEVEL));
  if (length > 0)
  {
    uint16_t tx_length = (length < (int)sizeof(report))
                       ? (uint16_t)length : (uint16_t)(sizeof(report) - 1U);
    app_bringup_uart_write(report, tx_length);
  }
}
#endif

#if APP_BRINGUP_STAGE >= 3U
static bool s_mcp_ok;
static bool s_dac_ok;
static uint16_t s_mcp_identity;
static uint8_t s_mcp_config[5];

#if APP_BRINGUP_STAGE == 3U
#define STAGE3_SWEEP_POINTS      100U
#define STAGE3_SWEEP_SETTLE_MS   250U

static bool s_dac_sweep_done;
static uint8_t s_dac_sweep_index;
static uint16_t s_dac_code;
static uint32_t s_dac_tick;

static uint16_t app_bringup_stage3_code_for_point(uint8_t index)
{
  uint32_t numerator = (uint32_t)index * UINT16_MAX
                     + ((STAGE3_SWEEP_POINTS - 1U) / 2U);
  return (uint16_t)(numerator / (STAGE3_SWEEP_POINTS - 1U));
}
#endif

static void app_bringup_stage3_init(void)
{
  uint8_t identity[2] = {0U};
  uint8_t index;

  s_mcp_ok = false;
  s_dac_ok = false;
#if APP_BRINGUP_STAGE == 3U
  s_dac_sweep_done = false;
  s_dac_sweep_index = 0U;
  s_dac_code = app_bringup_stage3_code_for_point(0U);
  s_dac_tick = HAL_GetTick();
#endif
  s_mcp_identity = 0U;

  if ((app_configure_spi() == HAL_OK)
      && (MCP3464_Init(&hspi2) == HAL_OK)
      && (MCP3464_ReadRegister(0x0EU, identity, sizeof(identity)) == HAL_OK))
  {
    s_mcp_identity = (uint16_t)(((uint16_t)identity[0] << 8) | identity[1]);
    s_mcp_ok = (s_mcp_identity == 0x000BU);
  }

  if (s_mcp_ok)
  {
    for (index = 0U; index < 5U; ++index)
    {
      if (MCP3464_ReadRegister((uint8_t)(MCP3464_REG_CONFIG0 + index),
                               &s_mcp_config[index], 1U) != HAL_OK)
      {
        s_mcp_ok = false;
      }
    }
    if (s_mcp_ok)
    {
      Measurements_Init();
    }
  }

  if (DAC8562_Init(&hspi1) == HAL_OK)
  {
    s_dac_ok = true;
  }
}

#if APP_BRINGUP_STAGE == 3U
static bool app_bringup_stage3_set_point(uint8_t index)
{
  uint16_t code = app_bringup_stage3_code_for_point(index);

  if (!s_mcp_ok || !s_dac_ok)
  {
    return false;
  }

  if ((DAC8562_SetCVRaw(code) == HAL_OK)
      && (DAC8562_SetCCRaw(code) == HAL_OK))
  {
    DAC8562_LdacPulse();
    s_dac_sweep_index = index;
    s_dac_code = code;
    return true;
  }
  return false;
}

static void app_bringup_stage3_report_sweep_point(void)
{
  const Measurements_Data_t *data = Measurements_GetData();
  int32_t expected_raw = (int32_t)(s_dac_code / 2U);
  uint32_t ideal_uV = (uint32_t)(((uint64_t)s_dac_code * 3000000ULL
                                  + (UINT16_MAX / 2U)) / UINT16_MAX);
  char report[180];
  int length;

  length = snprintf(report, sizeof(report),
                    "SWEEP N=%03u CODE=%05u IDEAL_UV=%07lu EXP=%ld "
                    "CC=%ld CC_ERR=%ld CV=%ld CV_ERR=%ld "
                    "CC_MV=%lu CV_MV=%lu\r\n",
                    (unsigned int)s_dac_sweep_index,
                    (unsigned int)s_dac_code, (unsigned long)ideal_uV,
                    (long)expected_raw, (long)data->dac_cc_readback_raw,
                    (long)(data->dac_cc_readback_raw - expected_raw),
                    (long)data->dac_cv_readback_raw,
                    (long)(data->dac_cv_readback_raw - expected_raw),
                    (unsigned long)data->dac_cc_readback_mV,
                    (unsigned long)data->dac_cv_readback_mV);
  if (length > 0)
  {
    uint16_t tx_length = (length < (int)sizeof(report))
                       ? (uint16_t)length : (uint16_t)(sizeof(report) - 1U);
    app_bringup_uart_write(report, tx_length);
  }
}

static void app_bringup_stage3_sweep_task(uint32_t now)
{
  static const char done[] = "SWEEP DONE; DAC outputs returned to 0 V\r\n";

  if (s_dac_sweep_done
      || ((uint32_t)(now - s_dac_tick) < STAGE3_SWEEP_SETTLE_MS))
  {
    return;
  }

  s_dac_tick = now;
  app_bringup_stage3_report_sweep_point();

  if (s_dac_sweep_index >= (STAGE3_SWEEP_POINTS - 1U))
  {
    (void)DAC8562_SetCVRaw(0U);
    (void)DAC8562_SetCCRaw(0U);
    DAC8562_LdacPulse();
    s_dac_sweep_done = true;
    app_bringup_uart_write(done, (uint16_t)(sizeof(done) - 1U));
  }
  else if (!app_bringup_stage3_set_point((uint8_t)(s_dac_sweep_index + 1U)))
  {
    static const char failed[] = "SWEEP FAIL: DAC write error\r\n";
    s_dac_sweep_done = true;
    app_bringup_uart_write(failed, (uint16_t)(sizeof(failed) - 1U));
  }
}
#endif

#if APP_BRINGUP_STAGE == 4U
static uint32_t app_bringup_temperature_magnitude(int32_t centi_C)
{
  return (uint32_t)((centi_C < 0) ? -(int64_t)centi_C : (int64_t)centi_C);
}

static char app_bringup_temperature_sign(int32_t centi_C)
{
  return (centi_C < 0) ? '-' : '+';
}

static void app_bringup_stage4_report(void)
{
  const Measurements_Data_t *data = Measurements_GetData();
  uint32_t temperature[MEASUREMENTS_TEMPERATURE_COUNT];
  GPIO_PinState pgood;
  char report[420];
  int length;
  uint8_t index;

  for (index = 0U; index < MEASUREMENTS_TEMPERATURE_COUNT; ++index)
  {
    temperature[index] =
        app_bringup_temperature_magnitude(data->temperature_centi_C[index]);
  }
  pgood = HAL_GPIO_ReadPin(PGOOD_5V_IN_GPIO_Port, PGOOD_5V_IN_Pin);

  length = snprintf(
      report, sizeof(report),
      "STM_ADC T1_MOS=%c%lu.%02luC T2_AMB=%c%lu.%02luC "
      "T3_BLEED=%c%lu.%02luC T4_PWR=%c%lu.%02luC PGOOD_5V=%u\r\n",
      app_bringup_temperature_sign(data->temperature_centi_C[0]),
      (unsigned long)(temperature[0] / 100U),
      (unsigned long)(temperature[0] % 100U),
      app_bringup_temperature_sign(data->temperature_centi_C[1]),
      (unsigned long)(temperature[1] / 100U),
      (unsigned long)(temperature[1] % 100U),
      app_bringup_temperature_sign(data->temperature_centi_C[2]),
      (unsigned long)(temperature[2] / 100U),
      (unsigned long)(temperature[2] % 100U),
      app_bringup_temperature_sign(data->temperature_centi_C[3]),
      (unsigned long)(temperature[3] / 100U),
      (unsigned long)(temperature[3] % 100U),
      (unsigned int)(pgood == PGOOD_ASSERTED_LEVEL));
  if (length > 0)
  {
    uint16_t tx_length = (length < (int)sizeof(report))
                       ? (uint16_t)length : (uint16_t)(sizeof(report) - 1U);
    app_bringup_uart_write(report, tx_length);
  }

  length = snprintf(
      report, sizeof(report),
      "MCP_ADC STATUS=%s ID=%04X CFG=%02X/%02X/%02X/%02X IRQ=%02X "
      "DAC=%s VIN=%lu.%03luV VOUT=%lu.%03luV "
      "IOUT=N/A(U18_MISSING) DAC_CC=%lu.%03luV DAC_CV=%lu.%03luV\r\n"
      "ADC_RAW STM=%u/%u/%u/%u MCP_VIN=%ld MCP_VOUT=%ld MCP_IOUT=%ld "
      "MCP_CC=%ld MCP_CV=%ld\r\n",
      s_mcp_ok ? "OK" : "FAIL", (unsigned int)s_mcp_identity,
      (unsigned int)s_mcp_config[0], (unsigned int)s_mcp_config[1],
      (unsigned int)s_mcp_config[2], (unsigned int)s_mcp_config[3],
      (unsigned int)s_mcp_config[4], s_dac_ok ? "OK" : "FAIL",
      (unsigned long)(data->vin_mV / 1000U),
      (unsigned long)(data->vin_mV % 1000U),
      (unsigned long)(data->vout_mV / 1000U),
      (unsigned long)(data->vout_mV % 1000U),
      (unsigned long)(data->dac_cc_readback_mV / 1000U),
      (unsigned long)(data->dac_cc_readback_mV % 1000U),
      (unsigned long)(data->dac_cv_readback_mV / 1000U),
      (unsigned long)(data->dac_cv_readback_mV % 1000U),
      (unsigned int)data->temperature_filtered[0],
      (unsigned int)data->temperature_filtered[1],
      (unsigned int)data->temperature_filtered[2],
      (unsigned int)data->temperature_filtered[3],
      (long)data->vin_diff_raw, (long)data->vout_diff_raw,
      (long)data->iout_diff_raw, (long)data->dac_cc_readback_raw,
      (long)data->dac_cv_readback_raw);
  if (length > 0)
  {
    uint16_t tx_length = (length < (int)sizeof(report))
                       ? (uint16_t)length : (uint16_t)(sizeof(report) - 1U);
    app_bringup_uart_write(report, tx_length);
  }
}
#endif
#endif

#else

static uint32_t s_control_tick;
static uint32_t s_telemetry_tick;

#endif

void APP_Init(void)
{
  uint32_t now;

  OutputCtrl_Init();
  Bleeder_Init();

#if APP_BRINGUP_STAGE != 0U
#if APP_BRINGUP_STAGE == 1U
  static const char banner[] = "\r\nLDO BRINGUP STAGE 1: LED+UART OK\r\n";
#elif APP_BRINGUP_STAGE == 2U
  static const char banner[] = "\r\nLDO BRINGUP STAGE 2: ADC1+PGOOD\r\n";
#elif APP_BRINGUP_STAGE == 3U
  static const char banner[] = "\r\nLDO BRINGUP STAGE 3: MCP3464+DAC8562\r\n";
#else
  static const char banner[] =
      "\r\nLDO BRINGUP STAGE 4: ALL ADC PHYSICAL UNITS; OUTPUT=OFF\r\n";
#endif

  app_bringup_led_init();
  now = HAL_GetTick();
  s_uart_tick = now;
#if APP_BRINGUP_STAGE >= 2U
  s_led_tick = now;
#endif
#if APP_BRINGUP_STAGE == 2U
  (void)HAL_ADCEx_Calibration_Start(&hadc1);
#elif APP_BRINGUP_STAGE >= 3U
  app_bringup_stage3_init();
#endif
  app_bringup_uart_write(banner, (uint16_t)(sizeof(banner) - 1U));
#else
  (void)app_configure_spi();
  (void)DAC8562_Init(&hspi1);
  /* Local ADC1 temperature acquisition remains available even if SPI2 fails. */
  Measurements_Init();

  Control_Init();
  UART_Protocol_Init(&huart2);

  now = HAL_GetTick();
  s_control_tick = now;
  s_telemetry_tick = now;
#endif
}

void APP_Task(void)
{
  uint32_t now;

#if APP_BRINGUP_STAGE == 1U
  static const char heartbeat[] = "LDO BRINGUP STAGE 1: LED+UART OK\r\n";

  now = HAL_GetTick();
  if ((uint32_t)(now - s_uart_tick) >= 1000U)
  {
    s_uart_tick = now;
    app_bringup_uart_write(heartbeat, (uint16_t)(sizeof(heartbeat) - 1U));
  }
#elif APP_BRINGUP_STAGE == 2U
  now = HAL_GetTick();
  if ((uint32_t)(now - s_led_tick) >= 500U)
  {
    s_led_tick = now;
    HAL_GPIO_TogglePin(BOARD_LED_GPIO_PORT, BOARD_LED_PIN);
  }
  if ((uint32_t)(now - s_uart_tick) >= 1000U)
  {
    s_uart_tick = now;
    app_bringup_send_adc_report();
  }
#elif APP_BRINGUP_STAGE == 3U
  now = HAL_GetTick();
  if ((uint32_t)(now - s_led_tick) >= 500U)
  {
    s_led_tick = now;
    HAL_GPIO_TogglePin(BOARD_LED_GPIO_PORT, BOARD_LED_PIN);
  }
  if (s_mcp_ok)
  {
    Measurements_Task();
  }
  app_bringup_stage3_sweep_task(now);
#elif APP_BRINGUP_STAGE == 4U
  now = HAL_GetTick();
  if ((uint32_t)(now - s_led_tick) >= 500U)
  {
    s_led_tick = now;
    HAL_GPIO_TogglePin(BOARD_LED_GPIO_PORT, BOARD_LED_PIN);
  }
  if (s_mcp_ok)
  {
    Measurements_Task();
  }
  if ((uint32_t)(now - s_uart_tick) >= 1000U)
  {
    s_uart_tick = now;
    app_bringup_stage4_report();
  }
#else
  uint8_t catch_up = 0U;

  Measurements_Task();
  UART_Protocol_Task();

  now = HAL_GetTick();
  while (((uint32_t)(now - s_control_tick) >= APP_CONTROL_PERIOD_MS) && (catch_up < 10U))
  {
    s_control_tick += APP_CONTROL_PERIOD_MS;
    Control_Task1ms();
    Bleeder_Task1ms();
    ++catch_up;
  }
  if ((uint32_t)(now - s_control_tick) >= APP_CONTROL_PERIOD_MS)
  {
    /* Bound debugger/overload catch-up so the cooperative loop cannot starve. */
    s_control_tick = now;
  }

  if ((uint32_t)(now - s_telemetry_tick) >= APP_TELEMETRY_PERIOD_MS)
  {
    s_telemetry_tick = now;
    UART_Protocol_QueueTelemetry();
  }

  /* Start a frame queued by the parser or telemetry producer without waiting a loop. */
  UART_Protocol_Task();
#endif
}
