#ifndef MCP3464_H
#define MCP3464_H

#include "stm32g0xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  MCP3464_REG_ADCDATA  = 0x00U,
  MCP3464_REG_CONFIG0  = 0x01U,
  MCP3464_REG_CONFIG1  = 0x02U,
  MCP3464_REG_CONFIG2  = 0x03U,
  MCP3464_REG_CONFIG3  = 0x04U,
  MCP3464_REG_IRQ      = 0x05U,
  MCP3464_REG_MUX      = 0x06U,
  MCP3464_REG_SCAN     = 0x07U,
  MCP3464_REG_TIMER    = 0x08U,
  MCP3464_REG_OFFSETCAL = 0x09U,
  MCP3464_REG_GAINCAL  = 0x0AU,
  MCP3464_REG_LOCK     = 0x0DU,
  MCP3464_REG_CRCCFG   = 0x0FU
} MCP3464_Register_t;

/*
 * U11 MCP3464 channel wiring from Schematic_Prints.PDF:
 * differential power-path measurements use the corresponding P/N pair,
 * while DAC_CC and DAC_CV are measured single-ended against AGND.
 */
typedef enum
{
  MCP3464_CHANNEL_VOUT_P = 0U,
  MCP3464_CHANNEL_VOUT_N = 1U,
  MCP3464_CHANNEL_DAC_CC = 2U,
  MCP3464_CHANNEL_DAC_CV = 3U,
  MCP3464_CHANNEL_IOUT_P = 4U,
  MCP3464_CHANNEL_IOUT_N = 5U,
  MCP3464_CHANNEL_VIN_P  = 6U,
  MCP3464_CHANNEL_VIN_N  = 7U
} MCP3464_BoardChannel_t;

HAL_StatusTypeDef MCP3464_Init(SPI_HandleTypeDef *hspi);
HAL_StatusTypeDef MCP3464_Reset(void);
HAL_StatusTypeDef MCP3464_ReadRegister(uint8_t reg, uint8_t *data, uint8_t length);
HAL_StatusTypeDef MCP3464_WriteRegister(uint8_t reg, const uint8_t *data, uint8_t length);
HAL_StatusTypeDef MCP3464_SelectDifferential(uint8_t ch_plus, uint8_t ch_minus);
HAL_StatusTypeDef MCP3464_SelectSingleEnded(uint8_t ch_plus);
HAL_StatusTypeDef MCP3464_ReadConversion(int32_t *raw);

bool MCP3464_TakeDataReadyFlag(void);
void MCP3464_DataReadyFlagFromISR(void);

#endif /* MCP3464_H */
