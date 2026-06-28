#include "app.h"

#include "app_config.h"
#include "bleeder.h"
#include "control.h"
#include "dac8562.h"
#include "measurements.h"
#include "output_ctrl.h"
#include "spi.h"
#include "uart_protocol.h"
#include "usart.h"

#include <stdint.h>

static uint32_t s_control_tick;
static uint32_t s_telemetry_tick;

static HAL_StatusTypeDef app_configure_spi(void)
{
  HAL_StatusTypeDef spi1_status;
  HAL_StatusTypeDef spi2_status;

  /*
   * Keep the running project safe even before CubeMX regenerates from the
   * corrected .ioc: DAC8562 is SPI mode 1, MCP3464R is SPI mode 0.
   */
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2; /* 32 MHz, DAC max 50 MHz. */
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  spi1_status = HAL_SPI_Init(&hspi1);

  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4; /* 16 MHz, ADC max 20 MHz. */
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  spi2_status = HAL_SPI_Init(&hspi2);
  return ((spi1_status == HAL_OK) && (spi2_status == HAL_OK)) ? HAL_OK : HAL_ERROR;
}

void APP_Init(void)
{
  uint32_t now;

  OutputCtrl_Init();
  Bleeder_Init();

  (void)app_configure_spi();
  (void)DAC8562_Init(&hspi1);
  /* Local ADC1 temperature acquisition remains available even if SPI2 fails. */
  Measurements_Init();

  Control_Init();
  UART_Protocol_Init(&huart2);

  now = HAL_GetTick();
  s_control_tick = now;
  s_telemetry_tick = now;
}

void APP_Task(void)
{
  uint32_t now;
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
}
