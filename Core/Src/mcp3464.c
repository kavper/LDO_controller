#include "mcp3464.h"

#include "app_config.h"
#include "main.h"

#define MCP3464_COMMAND_STATIC_READ       0x01U
#define MCP3464_COMMAND_INCREMENTAL_WRITE 0x02U
#define MCP3464_FAST_FULL_RESET           0x0EU
#define MCP3464_MUX_AGND                  0x08U
#define MCP3464_MAX_REGISTER_BYTES        4U

static SPI_HandleTypeDef *s_spi;
static volatile bool s_data_ready;

static uint8_t mcp3464_command(uint8_t address, uint8_t type)
{
  return (uint8_t)(((MCP3464_DEVICE_ADDRESS & 0x03U) << 6)
                 | ((address & 0x0FU) << 2)
                 | (type & 0x03U));
}

static void mcp3464_select(void)
{
  HAL_GPIO_WritePin(SPI2_CS_ADC_GPIO_Port, SPI2_CS_ADC_Pin, GPIO_PIN_RESET);
}

static void mcp3464_deselect(void)
{
  HAL_GPIO_WritePin(SPI2_CS_ADC_GPIO_Port, SPI2_CS_ADC_Pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef mcp3464_fast_command(uint8_t fast_command)
{
  uint8_t command;
  HAL_StatusTypeDef status;

  if (s_spi == NULL)
  {
    return HAL_ERROR;
  }

  command = mcp3464_command(fast_command, 0U);
  mcp3464_select();
  status = HAL_SPI_Transmit(s_spi, &command, 1U, MCP3464_SPI_TIMEOUT_MS);
  mcp3464_deselect();
  return status;
}

HAL_StatusTypeDef MCP3464_Reset(void)
{
  s_data_ready = false;
  return mcp3464_fast_command(MCP3464_FAST_FULL_RESET);
}

HAL_StatusTypeDef MCP3464_ReadRegister(uint8_t reg, uint8_t *data, uint8_t length)
{
  uint8_t tx[MCP3464_MAX_REGISTER_BYTES + 1U] = {0U};
  uint8_t rx[MCP3464_MAX_REGISTER_BYTES + 1U] = {0U};
  HAL_StatusTypeDef status;
  uint8_t index;

  if ((s_spi == NULL) || (data == NULL) || (length == 0U)
      || (length > MCP3464_MAX_REGISTER_BYTES) || (reg > 0x0FU))
  {
    return HAL_ERROR;
  }

  tx[0] = mcp3464_command(reg, MCP3464_COMMAND_STATIC_READ);
  mcp3464_select();
  status = HAL_SPI_TransmitReceive(s_spi, tx, rx, (uint16_t)(length + 1U),
                                  MCP3464_SPI_TIMEOUT_MS);
  mcp3464_deselect();

  if (status == HAL_OK)
  {
    for (index = 0U; index < length; ++index)
    {
      data[index] = rx[index + 1U];
    }
  }
  return status;
}

HAL_StatusTypeDef MCP3464_WriteRegister(uint8_t reg, const uint8_t *data, uint8_t length)
{
  uint8_t tx[MCP3464_MAX_REGISTER_BYTES + 1U];
  HAL_StatusTypeDef status;
  uint8_t index;

  if ((s_spi == NULL) || (data == NULL) || (length == 0U)
      || (length > MCP3464_MAX_REGISTER_BYTES) || (reg > 0x0DU))
  {
    return HAL_ERROR;
  }

  tx[0] = mcp3464_command(reg, MCP3464_COMMAND_INCREMENTAL_WRITE);
  for (index = 0U; index < length; ++index)
  {
    tx[index + 1U] = data[index];
  }

  mcp3464_select();
  status = HAL_SPI_Transmit(s_spi, tx, (uint16_t)(length + 1U), MCP3464_SPI_TIMEOUT_MS);
  mcp3464_deselect();
  return status;
}

HAL_StatusTypeDef MCP3464_SelectDifferential(uint8_t ch_plus, uint8_t ch_minus)
{
  uint8_t mux;

  if ((ch_plus > 7U) || (ch_minus > 7U))
  {
    return HAL_ERROR;
  }

  mux = (uint8_t)((ch_plus << 4) | ch_minus);
  /* A MUX write triggers the ADC's documented automatic reset/restart. */
  return MCP3464_WriteRegister(MCP3464_REG_MUX, &mux, 1U);
}

HAL_StatusTypeDef MCP3464_SelectSingleEnded(uint8_t ch_plus)
{
  uint8_t mux;

  if (ch_plus > 7U)
  {
    return HAL_ERROR;
  }

  mux = (uint8_t)((ch_plus << 4) | MCP3464_MUX_AGND);
  return MCP3464_WriteRegister(MCP3464_REG_MUX, &mux, 1U);
}

HAL_StatusTypeDef MCP3464_ReadConversion(int32_t *raw)
{
  uint8_t data[2];
  HAL_StatusTypeDef status;
  int16_t signed_code;

  if (raw == NULL)
  {
    return HAL_ERROR;
  }

  status = MCP3464_ReadRegister(MCP3464_REG_ADCDATA, data, sizeof(data));
  if (status == HAL_OK)
  {
    signed_code = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    *raw = (int32_t)signed_code;
  }
  return status;
}

bool MCP3464_TakeDataReadyFlag(void)
{
  bool ready;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  ready = s_data_ready;
  s_data_ready = false;
  __set_PRIMASK(primask);
  return ready;
}

void MCP3464_DataReadyFlagFromISR(void)
{
  s_data_ready = true;
}

HAL_StatusTypeDef MCP3464_Init(SPI_HandleTypeDef *hspi)
{
  HAL_StatusTypeDef status;
  const uint8_t gain_calibration[3] = {0x7AU, 0xC1U, 0x00U};
  uint8_t value;

  if (hspi == NULL)
  {
    return HAL_ERROR;
  }

  s_spi = hspi;
  s_data_ready = false;
  HAL_GPIO_WritePin(SPI2_CS_ADC_GPIO_Port, SPI2_CS_ADC_Pin, GPIO_PIN_SET);

  status = MCP3464_Reset();
  if (status != HAL_OK)
  {
    return status;
  }
  HAL_Delay(2U);

  /* CONFIG1: MCLK/1, OSR 256, reserved bits 00. */
  value = 0x0CU;
  status = MCP3464_WriteRegister(MCP3464_REG_CONFIG1, &value, 1U);
  if (status != HAL_OK)
  {
    return status;
  }

  /* CONFIG2: default boost, gain x1, MUX auto-zero off, reserved bits = 11. */
  value = 0x8BU;
  status = MCP3464_WriteRegister(MCP3464_REG_CONFIG2, &value, 1U);
  if (status != HAL_OK)
  {
    return status;
  }

  /*
   * Digital gain 31425/32768 = 0.9590149 leaves headroom for the measured
   * positive ADC gain error, so a 0..3 V DAC sweep does not saturate at 0x7FFF.
   */
  status = MCP3464_WriteRegister(MCP3464_REG_GAINCAL, gain_calibration,
                                 sizeof(gain_calibration));
  if (status != HAL_OK)
  {
    return status;
  }

  /*
   * CONFIG3: continuous conversion, signed 16-bit ADCDATA, communication CRC
   * off, digital gain calibration enabled.
   */
  value = 0xC1U;
  status = MCP3464_WriteRegister(MCP3464_REG_CONFIG3, &value, 1U);
  if (status != HAL_OK)
  {
    return status;
  }

  /* IRQ: push-pull inactive high, Fast commands enabled, start pulse disabled. */
  value = 0x06U;
  status = MCP3464_WriteRegister(MCP3464_REG_IRQ, &value, 1U);
  if (status != HAL_OK)
  {
    return status;
  }

  /*
   * CONFIG0 for MCP3464 (non-R): internal clock without clock output,
   * sensor-bias currents off, conversion mode. REFIN+ uses board 3V_REF.
   */
  value = 0xA3U;
  status = MCP3464_WriteRegister(MCP3464_REG_CONFIG0, &value, 1U);
  if (status != HAL_OK)
  {
    return status;
  }

  __HAL_GPIO_EXTI_CLEAR_IT(ADC_IRQ_MDAT_Pin);
  HAL_NVIC_SetPriority(EXTI4_15_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

  if (HAL_GPIO_ReadPin(ADC_IRQ_MDAT_GPIO_Port, ADC_IRQ_MDAT_Pin) == GPIO_PIN_RESET)
  {
    s_data_ready = true;
  }
  return HAL_OK;
}

void EXTI4_15_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(ADC_IRQ_MDAT_Pin);
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == ADC_IRQ_MDAT_Pin)
  {
    MCP3464_DataReadyFlagFromISR();
  }
}
