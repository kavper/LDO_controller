#include "dac8562.h"

#include "app_config.h"
#include "main.h"

#define DAC8562_COMMAND_WRITE_INPUT        0x00U
#define DAC8562_COMMAND_SOFTWARE_RESET     0x05U
#define DAC8562_COMMAND_INTERNAL_REFERENCE 0x07U
#define DAC8562_ADDRESS_A                  0x00U
#define DAC8562_ADDRESS_B                  0x01U

static SPI_HandleTypeDef *s_spi;

static void dac8562_select(void)
{
  HAL_GPIO_WritePin(SPI1_CS_DAC_GPIO_Port, SPI1_CS_DAC_Pin, GPIO_PIN_RESET);
}

static void dac8562_deselect(void)
{
  HAL_GPIO_WritePin(SPI1_CS_DAC_GPIO_Port, SPI1_CS_DAC_Pin, GPIO_PIN_SET);
}

HAL_StatusTypeDef DAC8562_RawWrite(uint8_t command, uint8_t address, uint16_t data)
{
  uint32_t word;
  uint8_t tx[3];
  HAL_StatusTypeDef status;

  if (s_spi == NULL)
  {
    return HAL_ERROR;
  }

  /* DB23:22 are don't-care, DB21:19 are command, DB18:16 are address. */
  word = (((uint32_t)command & 0x07U) << 19)
       | (((uint32_t)address & 0x07U) << 16)
       | (uint32_t)data;
  tx[0] = (uint8_t)(word >> 16);
  tx[1] = (uint8_t)(word >> 8);
  tx[2] = (uint8_t)word;

  dac8562_select();
  status = HAL_SPI_Transmit(s_spi, tx, sizeof(tx), DAC8562_SPI_TIMEOUT_MS);
  dac8562_deselect();
  return status;
}

void DAC8562_LdacPulse(void)
{
  HAL_GPIO_WritePin(DAC_LDAC_GPIO_Port, DAC_LDAC_Pin, GPIO_PIN_RESET);
  __NOP();
  __NOP();
  HAL_GPIO_WritePin(DAC_LDAC_GPIO_Port, DAC_LDAC_Pin, GPIO_PIN_SET);
}

HAL_StatusTypeDef DAC8562_SetCVRaw(uint16_t raw)
{
  return DAC8562_RawWrite(DAC8562_COMMAND_WRITE_INPUT, DAC8562_ADDRESS_A, raw);
}

HAL_StatusTypeDef DAC8562_SetCCRaw(uint16_t raw)
{
  return DAC8562_RawWrite(DAC8562_COMMAND_WRITE_INPUT, DAC8562_ADDRESS_B, raw);
}

HAL_StatusTypeDef DAC8562_Init(SPI_HandleTypeDef *hspi)
{
  HAL_StatusTypeDef status;

  if (hspi == NULL)
  {
    return HAL_ERROR;
  }

  s_spi = hspi;
  HAL_GPIO_WritePin(SPI1_CS_DAC_GPIO_Port, SPI1_CS_DAC_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(DAC_LDAC_GPIO_Port, DAC_LDAC_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(DAC_CLR_GPIO_Port, DAC_CLR_Pin, GPIO_PIN_SET);

  /* Full software reset (DB0 = 1) restores all control registers. */
  status = DAC8562_RawWrite(DAC8562_COMMAND_SOFTWARE_RESET, 0U, 1U);
  if (status != HAL_OK)
  {
    return status;
  }

#if DAC8562_USE_INTERNAL_REFERENCE
  /* Command 111, DB0 = 1 enables the 2.5 V internal reference. */
  status = DAC8562_RawWrite(DAC8562_COMMAND_INTERNAL_REFERENCE, 0U, 1U);
  if (status != HAL_OK)
  {
    return status;
  }
#endif

  /* Buffer both zero codes, then update channel A (CV) and B (CC) together. */
  status = DAC8562_SetCVRaw(0U);
  if (status == HAL_OK)
  {
    status = DAC8562_SetCCRaw(0U);
  }
  if (status == HAL_OK)
  {
    DAC8562_LdacPulse();
  }
  return status;
}
