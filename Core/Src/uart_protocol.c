#include "uart_protocol.h"

#include "app_config.h"
#include "bleeder.h"
#include "control.h"
#include "main.h"
#include "measurements.h"
#include "usart.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define UART_MIN_LENGTH              2U
#define UART_MAX_PAYLOAD             80U
#define UART_MAX_LENGTH              (UART_MIN_LENGTH + UART_MAX_PAYLOAD)
#define UART_MAX_FRAME               (2U + 1U + UART_MAX_LENGTH + 2U)
#define UART_RX_RING_SIZE            256U
#define UART_TX_QUEUE_DEPTH          4U
#define UART_TX_BUFFER_SIZE          1536U
#define UART_TEXT_LINE_SIZE          96U
#define UART_TEXT_LINE_QUEUE_DEPTH   4U

typedef enum
{
  RX_WAIT_SOF1 = 0U,
  RX_WAIT_SOF2,
  RX_WAIT_LENGTH,
  RX_WAIT_BODY,
  RX_WAIT_CRC_LOW,
  RX_WAIT_CRC_HIGH
} RxState_t;

typedef struct
{
  uint8_t data[UART_TX_BUFFER_SIZE];
  uint16_t length;
} TxFrame_t;

typedef enum
{
  UART_MODE_BINARY = 0U,
  UART_MODE_TEXT
} UartMode_t;

static UART_HandleTypeDef *s_uart;
static uint8_t s_uart_rx_byte;
static uint8_t s_rx_ring[UART_RX_RING_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile bool s_rx_rearm_pending;

static TxFrame_t s_tx_queue[UART_TX_QUEUE_DEPTH];
static uint8_t s_tx_head;
static uint8_t s_tx_tail;
static bool s_tx_active;
static volatile bool s_tx_complete;

static RxState_t s_rx_state;
static uint8_t s_rx_length;
static uint8_t s_rx_body[UART_MAX_LENGTH];
static uint8_t s_rx_body_index;
static uint16_t s_rx_crc;
static uint16_t s_rx_received_crc;
static uint8_t s_telemetry_sequence;
static UartMode_t s_mode;
static char s_text_build[UART_TEXT_LINE_SIZE];
static uint8_t s_text_build_length;
static char s_text_lines[UART_TEXT_LINE_QUEUE_DEPTH][UART_TEXT_LINE_SIZE];
static uint8_t s_text_line_head;
static uint8_t s_text_line_tail;

static uint16_t uart_crc16_update(uint16_t crc, uint8_t byte)
{
  uint8_t bit;

  crc ^= (uint16_t)byte << 8;
  for (bit = 0U; bit < 8U; ++bit)
  {
    crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U)
                          : (uint16_t)(crc << 1);
  }
  return crc;
}

static void uart_put_u16_le(uint8_t *buffer, uint16_t *index, uint16_t value)
{
  buffer[(*index)++] = (uint8_t)value;
  buffer[(*index)++] = (uint8_t)(value >> 8);
}

static void uart_put_u32_le(uint8_t *buffer, uint16_t *index, uint32_t value)
{
  buffer[(*index)++] = (uint8_t)value;
  buffer[(*index)++] = (uint8_t)(value >> 8);
  buffer[(*index)++] = (uint8_t)(value >> 16);
  buffer[(*index)++] = (uint8_t)(value >> 24);
}

static uint32_t uart_get_u32_le(const uint8_t *buffer)
{
  return (uint32_t)buffer[0]
       | ((uint32_t)buffer[1] << 8)
       | ((uint32_t)buffer[2] << 16)
       | ((uint32_t)buffer[3] << 24);
}

static bool uart_queue_frame(uint8_t type, uint8_t sequence,
                             const uint8_t *payload, uint8_t payload_length)
{
  uint8_t next_head = (uint8_t)((s_tx_head + 1U) % UART_TX_QUEUE_DEPTH);
  TxFrame_t *frame;
  uint16_t index = 0U;
  uint16_t crc = 0xFFFFU;
  uint8_t length;
  uint8_t payload_index;

  if ((payload_length > UART_MAX_PAYLOAD) || (next_head == s_tx_tail))
  {
    return false;
  }

  length = (uint8_t)(payload_length + UART_MIN_LENGTH);
  frame = &s_tx_queue[s_tx_head];
  frame->data[index++] = UART_PROTOCOL_SOF1;
  frame->data[index++] = UART_PROTOCOL_SOF2;
  frame->data[index++] = length;
  frame->data[index++] = type;
  frame->data[index++] = sequence;

  crc = uart_crc16_update(crc, length);
  crc = uart_crc16_update(crc, type);
  crc = uart_crc16_update(crc, sequence);
  for (payload_index = 0U; payload_index < payload_length; ++payload_index)
  {
    uint8_t byte = payload[payload_index];
    frame->data[index++] = byte;
    crc = uart_crc16_update(crc, byte);
  }
  uart_put_u16_le(frame->data, &index, crc);
  frame->length = index;
  s_tx_head = next_head;
  return true;
}

static void uart_queue_ack(uint8_t sequence, uint8_t acknowledged_type)
{
  (void)uart_queue_frame(UART_PROTOCOL_ACK, sequence, &acknowledged_type, 1U);
}

static void uart_queue_nack(uint8_t sequence, uint8_t rejected_type,
                            UART_ProtocolNackReason_t reason)
{
  uint8_t payload[2] = {rejected_type, (uint8_t)reason};
  (void)uart_queue_frame(UART_PROTOCOL_NACK, sequence, payload, sizeof(payload));
}

static void uart_dispatch_frame(void)
{
  uint8_t type = s_rx_body[0];
  uint8_t sequence = s_rx_body[1];
  const uint8_t *payload = &s_rx_body[2];
  uint8_t payload_length = (uint8_t)(s_rx_length - UART_MIN_LENGTH);

  switch (type)
  {
    case UART_PROTOCOL_SET_VOLTAGE:
      if (payload_length != 4U)
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        break;
      }
      Control_SetVoltageTarget(uart_get_u32_le(payload));
      uart_queue_ack(sequence, type);
      break;

    case UART_PROTOCOL_SET_CURRENT:
      if (payload_length != 4U)
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        break;
      }
      Control_SetCurrentTarget(uart_get_u32_le(payload));
      uart_queue_ack(sequence, type);
      break;

    case UART_PROTOCOL_SET_OUTPUT:
      if ((payload_length != 1U) || (payload[0] > 1U))
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        break;
      }
      Control_SetOutputEnabled(payload[0] != 0U);
      uart_queue_ack(sequence, type);
      break;

    case UART_PROTOCOL_PING:
      if (payload_length != 0U)
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        break;
      }
      uart_queue_ack(sequence, type);
      break;

    default:
      uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_UNKNOWN_TYPE);
      break;
  }
}

static void uart_parser_reset(void)
{
  s_rx_state = RX_WAIT_SOF1;
  s_rx_length = 0U;
  s_rx_body_index = 0U;
  s_rx_crc = 0xFFFFU;
  s_rx_received_crc = 0U;
}

static void uart_parse_byte(uint8_t byte)
{
  switch (s_rx_state)
  {
    case RX_WAIT_SOF1:
      if (byte == UART_PROTOCOL_SOF1)
      {
        s_rx_state = RX_WAIT_SOF2;
      }
      break;

    case RX_WAIT_SOF2:
      if (byte == UART_PROTOCOL_SOF2)
      {
        s_rx_state = RX_WAIT_LENGTH;
      }
      else if (byte != UART_PROTOCOL_SOF1)
      {
        s_rx_state = RX_WAIT_SOF1;
      }
      break;

    case RX_WAIT_LENGTH:
      if ((byte < UART_MIN_LENGTH) || (byte > UART_MAX_LENGTH))
      {
        uart_parser_reset();
        break;
      }
      s_rx_length = byte;
      s_rx_body_index = 0U;
      s_rx_crc = uart_crc16_update(0xFFFFU, byte);
      s_rx_state = RX_WAIT_BODY;
      break;

    case RX_WAIT_BODY:
      s_rx_body[s_rx_body_index++] = byte;
      s_rx_crc = uart_crc16_update(s_rx_crc, byte);
      if (s_rx_body_index >= s_rx_length)
      {
        s_rx_state = RX_WAIT_CRC_LOW;
      }
      break;

    case RX_WAIT_CRC_LOW:
      s_rx_received_crc = byte;
      s_rx_state = RX_WAIT_CRC_HIGH;
      break;

    case RX_WAIT_CRC_HIGH:
      s_rx_received_crc |= (uint16_t)byte << 8;
      if (s_rx_received_crc == s_rx_crc)
      {
        uart_dispatch_frame();
      }
      uart_parser_reset();
      break;

    default:
      uart_parser_reset();
      break;
  }
}

static void uart_parse_text_byte(uint8_t byte)
{
  if ((byte == '\r') || (byte == '\n'))
  {
    uint8_t next_head;

    if (s_text_build_length == 0U)
    {
      return;
    }
    next_head = (uint8_t)((s_text_line_head + 1U)
                          % UART_TEXT_LINE_QUEUE_DEPTH);
    if (next_head != s_text_line_tail)
    {
      s_text_build[s_text_build_length] = '\0';
      memcpy(s_text_lines[s_text_line_head], s_text_build,
             (size_t)s_text_build_length + 1U);
      s_text_line_head = next_head;
    }
    s_text_build_length = 0U;
    return;
  }

  if ((byte == '\b') || (byte == 0x7FU))
  {
    if (s_text_build_length > 0U)
    {
      --s_text_build_length;
    }
    return;
  }

  if ((byte >= 0x20U) && (byte <= 0x7EU)
      && (s_text_build_length < (UART_TEXT_LINE_SIZE - 1U)))
  {
    s_text_build[s_text_build_length++] = (char)byte;
  }
}

static void uart_rx_task(void)
{
  while (s_rx_tail != s_rx_head)
  {
    uint8_t byte = s_rx_ring[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % UART_RX_RING_SIZE);
    if (s_mode == UART_MODE_TEXT)
    {
      uart_parse_text_byte(byte);
    }
    else
    {
      uart_parse_byte(byte);
    }
  }

  if (s_rx_rearm_pending && (s_uart != NULL))
  {
    if (HAL_UART_Receive_IT(s_uart, &s_uart_rx_byte, 1U) == HAL_OK)
    {
      s_rx_rearm_pending = false;
    }
  }
}

static void uart_tx_task(void)
{
  if (s_tx_active && s_tx_complete)
  {
    s_tx_complete = false;
    s_tx_active = false;
    s_tx_tail = (uint8_t)((s_tx_tail + 1U) % UART_TX_QUEUE_DEPTH);
  }

  if (!s_tx_active && (s_tx_tail != s_tx_head) && (s_uart != NULL))
  {
    TxFrame_t *frame = &s_tx_queue[s_tx_tail];
    s_tx_complete = false;
    s_tx_active = true;
    if (HAL_UART_Transmit_IT(s_uart, frame->data, frame->length) != HAL_OK)
    {
      s_tx_active = false;
    }
  }
}

void UART_Protocol_QueueTelemetry(void)
{
  const Measurements_Data_t *measurements = Measurements_GetData();
  const Control_Status_t *control = Control_GetStatus();
  uint8_t payload[56];
  uint16_t index = 0U;
  uint8_t temperature;

  uart_put_u32_le(payload, &index, measurements->vout_mV);
  uart_put_u32_le(payload, &index, measurements->iout_mA);
  uart_put_u32_le(payload, &index, measurements->vin_mV);
  uart_put_u32_le(payload, &index, measurements->dac_cv_readback_mV);
  uart_put_u32_le(payload, &index, measurements->dac_cc_readback_mV);
  uart_put_u32_le(payload, &index, control->voltage_target_mV);
  uart_put_u32_le(payload, &index, control->current_target_mA);
  uart_put_u32_le(payload, &index, control->vpre_request_mV);
  payload[index++] = (uint8_t)control->mode;
  payload[index++] = control->output_enabled ? 1U : 0U;
  payload[index++] = Bleeder_IsEnabled() ? 1U : 0U;
  payload[index++] = (HAL_GPIO_ReadPin(PGOOD_5V_IN_GPIO_Port, PGOOD_5V_IN_Pin)
                      == PGOOD_ASSERTED_LEVEL) ? 1U : 0U;
  uart_put_u32_le(payload, &index, 0U); /* Fault flags placeholder. */

  for (temperature = 0U; temperature < MEASUREMENTS_TEMPERATURE_COUNT; ++temperature)
  {
    uart_put_u16_le(payload, &index, measurements->temperature_raw[temperature]);
  }
  for (temperature = 0U; temperature < MEASUREMENTS_TEMPERATURE_COUNT; ++temperature)
  {
    uart_put_u16_le(payload, &index, measurements->temperature_filtered[temperature]);
  }

  (void)uart_queue_frame(UART_PROTOCOL_TELEMETRY, s_telemetry_sequence++, payload,
                         (uint8_t)index);
}

static void uart_init(UART_HandleTypeDef *huart, UartMode_t mode)
{
  s_uart = huart;
  s_mode = mode;
  s_rx_head = 0U;
  s_rx_tail = 0U;
  s_rx_rearm_pending = false;
  s_tx_head = 0U;
  s_tx_tail = 0U;
  s_tx_active = false;
  s_tx_complete = false;
  s_telemetry_sequence = 0U;
  s_text_build_length = 0U;
  s_text_line_head = 0U;
  s_text_line_tail = 0U;
  memset(s_tx_queue, 0, sizeof(s_tx_queue));
  memset(s_text_lines, 0, sizeof(s_text_lines));
  uart_parser_reset();

  HAL_NVIC_SetPriority(USART2_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
  if ((s_uart == NULL) || (HAL_UART_Receive_IT(s_uart, &s_uart_rx_byte, 1U) != HAL_OK))
  {
    s_rx_rearm_pending = true;
  }
}

void UART_Protocol_Init(UART_HandleTypeDef *huart)
{
  uart_init(huart, UART_MODE_BINARY);
}

void UART_Protocol_InitText(UART_HandleTypeDef *huart)
{
  uart_init(huart, UART_MODE_TEXT);
}

bool UART_Protocol_QueueText(const char *text)
{
  uint8_t next_head;
  TxFrame_t *frame;
  size_t length;

  if ((text == NULL) || (s_mode != UART_MODE_TEXT))
  {
    return false;
  }
  length = strlen(text);
  if ((length == 0U) || (length >= UART_TX_BUFFER_SIZE))
  {
    return false;
  }
  next_head = (uint8_t)((s_tx_head + 1U) % UART_TX_QUEUE_DEPTH);
  if (next_head == s_tx_tail)
  {
    return false;
  }
  frame = &s_tx_queue[s_tx_head];
  memcpy(frame->data, text, length);
  frame->length = (uint16_t)length;
  s_tx_head = next_head;
  return true;
}

bool UART_Protocol_ReadLine(char *line, size_t capacity)
{
  size_t length;

  if ((line == NULL) || (capacity == 0U)
      || (s_mode != UART_MODE_TEXT)
      || (s_text_line_tail == s_text_line_head))
  {
    return false;
  }
  length = strlen(s_text_lines[s_text_line_tail]);
  if (length >= capacity)
  {
    length = capacity - 1U;
  }
  memcpy(line, s_text_lines[s_text_line_tail], length);
  line[length] = '\0';
  s_text_line_tail = (uint8_t)((s_text_line_tail + 1U)
                               % UART_TEXT_LINE_QUEUE_DEPTH);
  return true;
}

void UART_Protocol_Task(void)
{
  uart_rx_task();
  uart_tx_task();
}

void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart2);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((s_uart != NULL) && (huart == s_uart))
  {
    uint16_t next_head = (uint16_t)((s_rx_head + 1U) % UART_RX_RING_SIZE);
    if (next_head != s_rx_tail)
    {
      s_rx_ring[s_rx_head] = s_uart_rx_byte;
      s_rx_head = next_head;
    }
    if (HAL_UART_Receive_IT(s_uart, &s_uart_rx_byte, 1U) != HAL_OK)
    {
      s_rx_rearm_pending = true;
    }
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((s_uart != NULL) && (huart == s_uart))
  {
    s_tx_complete = true;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((s_uart != NULL) && (huart == s_uart))
  {
    s_rx_rearm_pending = true;
  }
}
