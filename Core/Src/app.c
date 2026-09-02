#include "app.h"

#include "app_config.h"
#include "bleeder.h"
#include "board_led.h"
#include "control.h"
#include "dac8562.h"
#include "measurements.h"
#include "mcp3464.h"
#include "output_ctrl.h"
#include "spi.h"
#include "uart_console.h"
#include "uart_protocol.h"
#include "usart.h"

#include <stdbool.h>
#include <stdint.h>

static bool s_mcp_ok;
static bool s_dac_ok;
static uint32_t s_control_tick;
static uint32_t s_tlm_tick;

static HAL_StatusTypeDef app_configure_spi(void)
{
  HAL_StatusTypeDef spi1_status;
  HAL_StatusTypeDef spi2_status;

  /* DAC8562 SPI mode 1; MCP3464 SPI mode 0. */
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  spi1_status = HAL_SPI_Init(&hspi1);

  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  spi2_status = HAL_SPI_Init(&hspi2);
  return ((spi1_status == HAL_OK) && (spi2_status == HAL_OK)) ? HAL_OK : HAL_ERROR;
}

static void app_init_converters(void)
{
  uint8_t identity[2] = {0U};

  s_mcp_ok = false;
  s_dac_ok = false;

  if ((app_configure_spi() == HAL_OK)
      && (MCP3464_Init(&hspi2) == HAL_OK)
      && (MCP3464_ReadRegister(0x0EU, identity, sizeof(identity)) == HAL_OK)
      && ((((uint16_t)identity[0] << 8) | identity[1]) == 0x000BU))
  {
    s_mcp_ok = true;
    Measurements_Init();
  }

  if (DAC8562_Init(&hspi1) == HAL_OK)
  {
    s_dac_ok = true;
  }
}

void APP_Init(void)
{
  uint32_t now;

  OutputCtrl_Init();
  Bleeder_Init();
  BoardLed_Init();
  app_init_converters();
  Control_Init();
  UART_Protocol_InitText(&huart2);
  UART_Console_Init(s_mcp_ok, s_dac_ok);

  now = HAL_GetTick();
  s_control_tick = now;
  s_tlm_tick = now;
}

void APP_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t catch_up = 0U;

  BoardLed_Task(now);
  if (s_mcp_ok)
  {
    Measurements_Task();
  }
  UART_Protocol_Task();

  while (((uint32_t)(now - s_control_tick) >= APP_CONTROL_PERIOD_MS)
         && (catch_up < 10U))
  {
    s_control_tick += APP_CONTROL_PERIOD_MS;
    Control_Task1ms();
    Bleeder_Task1ms();
    ++catch_up;
  }
  if ((uint32_t)(now - s_control_tick) >= APP_CONTROL_PERIOD_MS)
  {
    s_control_tick = now;
  }

  UART_Console_Task(now);
  if ((uint32_t)(now - s_tlm_tick) >= CONSOLE_TLM_PERIOD_MS)
  {
    s_tlm_tick = now;
    UART_Console_QueueMachineTelemetry();
  }
  UART_Protocol_Task();
}
