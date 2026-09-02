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
 * U29 MCP3464T-E/NC (ADC DAC.SchDoc 2026-08-30):
 * CH0/1 = DAC CV/CC single-ended, CH2/3 VOUT, CH4/5 IOUT, CH6/7 VIN.
 */
typedef enum
{
  MCP3464_CHANNEL_DAC_CV = 0U, /* CH0 ADC_DAC_CV */
  MCP3464_CHANNEL_DAC_CC = 1U, /* CH1 ADC_DAC_CC */
  MCP3464_CHANNEL_VOUT_P = 2U, /* CH2 ADC_VOUT_P */
  MCP3464_CHANNEL_VOUT_N = 3U, /* CH3 ADC_VOUT_N */
  MCP3464_CHANNEL_IOUT_P = 4U, /* CH4 ADC_IOUT_P (3V_REFR) */
  MCP3464_CHANNEL_IOUT_N = 5U, /* CH5 ADC_IOUT_N (INA241 OUT) */
  MCP3464_CHANNEL_VIN_P  = 6U, /* CH6 ADC_VIN_P */
  MCP3464_CHANNEL_VIN_N  = 7U  /* CH7 ADC_VIN_N */
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
