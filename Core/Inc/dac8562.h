#ifndef DAC8562_H
#define DAC8562_H

#include "stm32g0xx_hal.h"
#include <stdint.h>

typedef enum
{
  DAC8562_CHANNEL_A_CV = 0U,
  DAC8562_CHANNEL_B_CC = 1U
} DAC8562_Channel_t;

HAL_StatusTypeDef DAC8562_Init(SPI_HandleTypeDef *hspi);
HAL_StatusTypeDef DAC8562_RawWrite(uint8_t command, uint8_t address, uint16_t data);
HAL_StatusTypeDef DAC8562_SetCVRaw(uint16_t raw);
HAL_StatusTypeDef DAC8562_SetCCRaw(uint16_t raw);
void DAC8562_LdacPulse(void);

#endif /* DAC8562_H */
