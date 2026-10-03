#include "uart_protocol.h"

#include "app_config.h"
#include "bleeder.h"
#include "control.h"
#include "fan_request.h"
#include "main.h"
#include "measurements.h"
#include "uart_console.h"
#include "usart.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define UART_MIN_LENGTH              2U
#define UART_MAX_PAYLOAD             80U
#define UART_MAX_LENGTH              (UART_MIN_LENGTH + UART_MAX_PAYLOAD)
#define UART_FRAME_MAX               120U
#define UART_RX_DMA_BYTES            256U
#define UART_FIFO_DEPTH              4U
#define UART_TLM_BYTES               68U
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

typedef enum
{
  UART_MODE_BINARY = 0U,
  UART_MODE_TEXT
} UartMode_t;

typedef enum
{
  UART_PRI_SAFETY = 0,
  UART_PRI_ACK,
  UART_PRI_FAST,
  UART_PRI_TEXT
} UartPri_t;

static UART_HandleTypeDef *s_uart;
static DMA_HandleTypeDef s_dma_rx;
static DMA_HandleTypeDef s_dma_tx;
static uint8_t s_rx_dma[UART_RX_DMA_BYTES];
static uint16_t s_rx_tail;
static volatile uint8_t s_rx_kick;

static uint8_t s_safety[UART_FIFO_DEPTH][UART_FRAME_MAX];
static uint16_t s_safety_len[UART_FIFO_DEPTH];
static uint8_t s_safety_head;
static uint8_t s_safety_tail;
static uint8_t s_safety_count;
static uint8_t s_ack[UART_FIFO_DEPTH][UART_FRAME_MAX];
static uint16_t s_ack_len[UART_FIFO_DEPTH];
static uint8_t s_ack_head;
static uint8_t s_ack_tail;
static uint8_t s_ack_count;
static uint8_t s_fast[UART_FRAME_MAX];
static uint16_t s_fast_len;
static bool s_fast_pending;
static uint8_t s_textq[UART_FIFO_DEPTH][UART_FRAME_MAX];
static uint16_t s_textq_len[UART_FIFO_DEPTH];
static uint8_t s_textq_head;
static uint8_t s_textq_tail;
static uint8_t s_textq_count;
static uint8_t s_tx[UART_FRAME_MAX];
static volatile bool s_tx_busy;
static volatile bool s_tx_done;

static uint8_t s_snap[2][UART_TLM_BYTES];
static uint8_t s_snap_len[2];
static volatile uint8_t s_snap_pub;

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
static bool s_done_valid;
static uint8_t s_done_type;
static uint8_t s_done_seq;
static bool s_done_ack;
static uint8_t s_done_reason;

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

static void uart_put_i16_le(uint8_t *buffer, uint16_t *index, int16_t value)
{
  uart_put_u16_le(buffer, index, (uint16_t)value);
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

static uint32_t uart_fault_flags(void)
{
  const char *fault = UART_Console_GetFault();

  if ((fault == NULL) || (strcmp(fault, "NONE") == 0))
  {
    return 0U;
  }
  if (strcmp(fault, "HW_INIT") == 0) return UART_PROTOCOL_FAULT_HW_INIT;
  if (strcmp(fault, "PGOOD_LOST") == 0) return UART_PROTOCOL_FAULT_PGOOD_LOST;
  if (strcmp(fault, "POWER_KILL") == 0) return UART_PROTOCOL_FAULT_POWER_KILL;
  if (strcmp(fault, "VIN_LOW") == 0) return UART_PROTOCOL_FAULT_VIN_LOW;
  if (strcmp(fault, "VOUT_HARD") == 0) return UART_PROTOCOL_FAULT_VOUT_HARD;
  if (strcmp(fault, "VOUT_HIGH") == 0) return UART_PROTOCOL_FAULT_VOUT_HIGH;
  if (strcmp(fault, "TEMP_HIGH") == 0) return UART_PROTOCOL_FAULT_TEMP_HIGH;
  if (strcmp(fault, "IOUT_HARD") == 0) return UART_PROTOCOL_FAULT_IOUT_HARD;
  return UART_PROTOCOL_FAULT_HW_INIT;
}

static bool uart_fifo_push(uint8_t queue[][UART_FRAME_MAX], uint16_t *lengths,
                           uint8_t *head, uint8_t *count,
                           const uint8_t *frame, uint16_t length)
{
  if ((*count >= UART_FIFO_DEPTH) || (length > UART_FRAME_MAX))
  {
    return false;
  }
  memcpy(queue[*head], frame, length);
  lengths[*head] = length;
  *head = (uint8_t)((*head + 1U) % UART_FIFO_DEPTH);
  (*count)++;
  return true;
}

static bool uart_fifo_pop(uint8_t queue[][UART_FRAME_MAX], uint16_t *lengths,
                          uint8_t *tail, uint8_t *count,
                          uint8_t *dst, uint16_t *out_len)
{
  if (*count == 0U)
  {
    return false;
  }
  memcpy(dst, queue[*tail], lengths[*tail]);
  *out_len = lengths[*tail];
  *tail = (uint8_t)((*tail + 1U) % UART_FIFO_DEPTH);
  (*count)--;
  return true;
}

static bool uart_submit(UartPri_t pri, const uint8_t *frame, uint16_t length)
{
  if ((frame == NULL) || (length == 0U) || (length > UART_FRAME_MAX))
  {
    return false;
  }
  if (pri == UART_PRI_SAFETY)
  {
    return uart_fifo_push(s_safety, s_safety_len, &s_safety_head,
                          &s_safety_count, frame, length);
  }
  if (pri == UART_PRI_ACK)
  {
    return uart_fifo_push(s_ack, s_ack_len, &s_ack_head, &s_ack_count,
                          frame, length);
  }
  if (pri == UART_PRI_TEXT)
  {
    return uart_fifo_push(s_textq, s_textq_len, &s_textq_head, &s_textq_count,
                          frame, length);
  }
  memcpy(s_fast, frame, length);
  s_fast_len = length;
  s_fast_pending = true;
  return true;
}

static bool uart_queue_frame(uint8_t type, uint8_t sequence,
                             const uint8_t *payload, uint8_t payload_length,
                             UartPri_t pri)
{
  uint8_t frame[UART_FRAME_MAX];
  uint16_t index = 0U;
  uint16_t crc = 0xFFFFU;
  uint8_t length;
  uint8_t payload_index;

  if (payload_length > UART_MAX_PAYLOAD)
  {
    return false;
  }

  length = (uint8_t)(payload_length + UART_MIN_LENGTH);
  frame[index++] = UART_PROTOCOL_SOF1;
  frame[index++] = UART_PROTOCOL_SOF2;
  frame[index++] = length;
  frame[index++] = type;
  frame[index++] = sequence;

  crc = uart_crc16_update(crc, length);
  crc = uart_crc16_update(crc, type);
  crc = uart_crc16_update(crc, sequence);
  for (payload_index = 0U; payload_index < payload_length; ++payload_index)
  {
    uint8_t byte = payload[payload_index];
    frame[index++] = byte;
    crc = uart_crc16_update(crc, byte);
  }
  uart_put_u16_le(frame, &index, crc);
  return uart_submit(pri, frame, index);
}

static void uart_queue_ack(uint8_t sequence, uint8_t acknowledged_type)
{
  (void)uart_queue_frame(UART_PROTOCOL_ACK, sequence, &acknowledged_type, 1U,
                         UART_PRI_ACK);
}

static void uart_queue_nack(uint8_t sequence, uint8_t rejected_type,
                            UART_ProtocolNackReason_t reason)
{
  uint8_t payload[2] = {rejected_type, (uint8_t)reason};
  (void)uart_queue_frame(UART_PROTOCOL_NACK, sequence, payload, sizeof(payload),
                         UART_PRI_ACK);
}

static void uart_remember(uint8_t type, uint8_t sequence, bool ack,
                          uint8_t reason)
{
  s_done_valid = true;
  s_done_type = type;
  s_done_seq = sequence;
  s_done_ack = ack;
  s_done_reason = reason;
}

static void uart_dispatch_frame(void)
{
  uint8_t type = s_rx_body[0];
  uint8_t sequence = s_rx_body[1];
  const uint8_t *payload = &s_rx_body[2];
  uint8_t payload_length = (uint8_t)(s_rx_length - UART_MIN_LENGTH);

  if (s_done_valid && (s_done_type == type) && (s_done_seq == sequence))
  {
    if (s_done_ack)
    {
      uart_queue_ack(sequence, type);
    }
    else
    {
      uart_queue_nack(sequence, type, (UART_ProtocolNackReason_t)s_done_reason);
    }
    return;
  }

  switch (type)
  {
    case UART_PROTOCOL_SET_VOLTAGE:
      if (payload_length != 4U)
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        uart_remember(type, sequence, false, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        break;
      }
      if (!UART_Console_ApplySetpoint(uart_get_u32_le(payload),
                                     Control_GetStatus()->current_target_mA))
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_RANGE);
        uart_remember(type, sequence, false, UART_PROTOCOL_NACK_RANGE);
      }
      else
      {
        uart_queue_ack(sequence, type);
        uart_remember(type, sequence, true, 0U);
      }
      break;

    case UART_PROTOCOL_SET_CURRENT:
      if (payload_length != 4U)
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        uart_remember(type, sequence, false, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        break;
      }
      if (!UART_Console_ApplySetpoint(Control_GetStatus()->voltage_target_mV,
                                     uart_get_u32_le(payload)))
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_RANGE);
        uart_remember(type, sequence, false, UART_PROTOCOL_NACK_RANGE);
      }
      else
      {
        uart_queue_ack(sequence, type);
        uart_remember(type, sequence, true, 0U);
      }
      break;

    case UART_PROTOCOL_SET_OUTPUT:
      if ((payload_length != 1U) || (payload[0] > 1U))
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        uart_remember(type, sequence, false, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        break;
      }
      if (UART_Console_SetOutput(payload[0] != 0U) != NULL)
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_UNSAFE);
        uart_remember(type, sequence, false, UART_PROTOCOL_NACK_UNSAFE);
      }
      else
      {
        uart_queue_ack(sequence, type);
        uart_remember(type, sequence, true, 0U);
      }
      break;

    case UART_PROTOCOL_PING:
      if (payload_length != 0U)
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        uart_remember(type, sequence, false, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        break;
      }
      uart_queue_ack(sequence, type);
      uart_remember(type, sequence, true, 0U);
      break;

    case UART_PROTOCOL_SETPOINT:
      if (payload_length != 8U)
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        uart_remember(type, sequence, false, UART_PROTOCOL_NACK_BAD_PAYLOAD);
        break;
      }
      if (!UART_Console_ApplySetpoint(uart_get_u32_le(&payload[0]),
                                     uart_get_u32_le(&payload[4])))
      {
        uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_RANGE);
        uart_remember(type, sequence, false, UART_PROTOCOL_NACK_RANGE);
      }
      else
      {
        uart_queue_ack(sequence, type);
        uart_remember(type, sequence, true, 0U);
      }
      break;

    default:
      uart_queue_nack(sequence, type, UART_PROTOCOL_NACK_UNKNOWN_TYPE);
      uart_remember(type, sequence, false, UART_PROTOCOL_NACK_UNKNOWN_TYPE);
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

static void uart_start_rx(void)
{
  if ((s_uart == NULL) || (s_uart->hdmarx == NULL))
  {
    return;
  }
  if (s_uart->RxState != HAL_UART_STATE_READY)
  {
    (void)HAL_UART_AbortReceive(s_uart);
  }
  s_rx_tail = 0U;
  if (HAL_UARTEx_ReceiveToIdle_DMA(s_uart, s_rx_dma, UART_RX_DMA_BYTES) == HAL_OK)
  {
    __HAL_DMA_ENABLE_IT(s_uart->hdmarx, DMA_IT_HT | DMA_IT_TC);
  }
}

static void uart_pump_tx(void)
{
  uint16_t length = 0U;
  bool have = false;

  if (s_uart == NULL)
  {
    return;
  }
  if (s_tx_busy)
  {
    if (!s_tx_done)
    {
      return;
    }
    s_tx_done = false;
    s_tx_busy = false;
  }

  if (s_safety_count > 0U)
  {
    have = uart_fifo_pop(s_safety, s_safety_len, &s_safety_tail,
                         &s_safety_count, s_tx, &length);
  }
  else if (s_ack_count > 0U)
  {
    have = uart_fifo_pop(s_ack, s_ack_len, &s_ack_tail, &s_ack_count,
                         s_tx, &length);
  }
  else if (s_fast_pending)
  {
    memcpy(s_tx, s_fast, s_fast_len);
    length = s_fast_len;
    s_fast_pending = false;
    have = true;
  }
  else if (s_textq_count > 0U)
  {
    have = uart_fifo_pop(s_textq, s_textq_len, &s_textq_tail, &s_textq_count,
                         s_tx, &length);
  }

  if (!have || (length == 0U))
  {
    return;
  }
  s_tx_done = false;
  s_tx_busy = true;
  if (HAL_UART_Transmit_DMA(s_uart, s_tx, length) != HAL_OK)
  {
    s_tx_busy = false;
  }
}

static void uart_rx_task(void)
{
  uint16_t guard = 0U;

  if ((s_uart == NULL) || (s_uart->hdmarx == NULL))
  {
    return;
  }
  while (guard < UART_RX_DMA_BYTES)
  {
    uint16_t remaining = (uint16_t)__HAL_DMA_GET_COUNTER(s_uart->hdmarx);
    uint16_t pos;

    if (remaining > UART_RX_DMA_BYTES)
    {
      remaining = UART_RX_DMA_BYTES;
    }
    pos = (uint16_t)(UART_RX_DMA_BYTES - remaining);
    if (s_rx_tail == pos)
    {
      break;
    }
    if (s_mode == UART_MODE_TEXT)
    {
      uart_parse_text_byte(s_rx_dma[s_rx_tail]);
    }
    else
    {
      uart_parse_byte(s_rx_dma[s_rx_tail]);
    }
    s_rx_tail = (uint16_t)((s_rx_tail + 1U) % UART_RX_DMA_BYTES);
    guard++;
  }
  s_rx_kick = 0U;
}

static uint8_t uart_fill_telemetry(uint8_t *payload)
{
  const Measurements_Data_t *measurements = Measurements_GetData();
  const Control_Status_t *control = Control_GetStatus();
  uint16_t index = 0U;
  uint8_t temperature;
  int16_t centi;

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
  uart_put_u32_le(payload, &index, uart_fault_flags());

  for (temperature = 0U; temperature < MEASUREMENTS_TEMPERATURE_COUNT; ++temperature)
  {
    uart_put_u16_le(payload, &index, measurements->temperature_raw[temperature]);
  }
  for (temperature = 0U; temperature < MEASUREMENTS_TEMPERATURE_COUNT; ++temperature)
  {
    uart_put_u16_le(payload, &index, measurements->temperature_filtered[temperature]);
  }
  for (temperature = 0U; temperature < MEASUREMENTS_TEMPERATURE_COUNT; ++temperature)
  {
    if ((measurements->temperature_centi_C[temperature] == INT32_MIN)
        || (measurements->temperature_centi_C[temperature] > 32767)
        || (measurements->temperature_centi_C[temperature] < -32767))
    {
      centi = INT16_MIN;
    }
    else
    {
      centi = (int16_t)measurements->temperature_centi_C[temperature];
    }
    uart_put_i16_le(payload, &index, centi);
  }
  payload[index++] = FanRequest_Percent();
  payload[index++] = (HAL_GPIO_ReadPin(POWER_KILL_GPIO_Port, POWER_KILL_Pin)
                      == POWER_KILL_ASSERTED_LEVEL) ? 1U : 0U;
  payload[index++] = (HAL_GPIO_ReadPin(CC_CV_STATE_GPIO_Port, CC_CV_STATE_Pin)
                      == CC_CV_STATE_CC_LEVEL) ? 1U : 0U;
  payload[index++] = (HAL_GPIO_ReadPin(OUT_OFF_GPIO_Port, OUT_OFF_Pin)
                      == OUT_OFF_ASSERTED_LEVEL) ? 1U : 0U;
  return (uint8_t)index;
}

static void uart_publish_telemetry(UartPri_t pri)
{
  uint8_t slot = (uint8_t)(s_snap_pub ^ 1U);
  uint32_t primask = __get_PRIMASK();
  uint8_t length;

  __disable_irq();
  length = uart_fill_telemetry(s_snap[slot]);
  s_snap_len[slot] = length;
  if (primask == 0U)
  {
    __enable_irq();
  }
  __DMB();
  s_snap_pub = slot;
  (void)uart_queue_frame(UART_PROTOCOL_TELEMETRY, s_telemetry_sequence++,
                         s_snap[slot], length, pri);
}

void UART_Protocol_QueueTelemetry(void)
{
  uart_publish_telemetry(UART_PRI_FAST);
}

void UART_Protocol_QueueFaultTelemetry(void)
{
  uart_publish_telemetry(UART_PRI_SAFETY);
}

static void uart_attach_dma(DMA_HandleTypeDef *dma, DMA_Channel_TypeDef *channel,
                            uint32_t request, uint32_t direction, uint32_t mode)
{
  dma->Instance = channel;
  dma->Init.Request = request;
  dma->Init.Direction = direction;
  dma->Init.PeriphInc = DMA_PINC_DISABLE;
  dma->Init.MemInc = DMA_MINC_ENABLE;
  dma->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  dma->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  dma->Init.Mode = mode;
  dma->Init.Priority = DMA_PRIORITY_HIGH;
  if (HAL_DMA_Init(dma) != HAL_OK)
  {
    Error_Handler();
  }
}

static void uart_init(UART_HandleTypeDef *huart, UartMode_t mode)
{
  s_uart = huart;
  s_mode = mode;
  s_rx_tail = 0U;
  s_rx_kick = 0U;
  s_safety_head = 0U;
  s_safety_tail = 0U;
  s_safety_count = 0U;
  s_ack_head = 0U;
  s_ack_tail = 0U;
  s_ack_count = 0U;
  s_fast_pending = false;
  s_textq_head = 0U;
  s_textq_tail = 0U;
  s_textq_count = 0U;
  s_tx_busy = false;
  s_tx_done = false;
  s_telemetry_sequence = 0U;
  s_text_build_length = 0U;
  s_text_line_head = 0U;
  s_text_line_tail = 0U;
  s_snap_pub = 0U;
  s_done_valid = false;
  uart_parser_reset();

  if (s_uart == NULL)
  {
    return;
  }

  __HAL_RCC_DMA1_CLK_ENABLE();
  uart_attach_dma(&s_dma_rx, DMA1_Channel2, DMA_REQUEST_USART2_RX,
                  DMA_PERIPH_TO_MEMORY, DMA_CIRCULAR);
  __HAL_LINKDMA(s_uart, hdmarx, s_dma_rx);
  uart_attach_dma(&s_dma_tx, DMA1_Channel3, DMA_REQUEST_USART2_TX,
                  DMA_MEMORY_TO_PERIPH, DMA_NORMAL);
  __HAL_LINKDMA(s_uart, hdmatx, s_dma_tx);
  HAL_NVIC_SetPriority(DMA1_Channel2_3_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);
  HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
  uart_start_rx();
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
  size_t length;
  size_t offset;

  if ((text == NULL) || (s_mode != UART_MODE_TEXT))
  {
    return false;
  }
  length = strlen(text);
  if (length == 0U)
  {
    return false;
  }
  for (offset = 0U; offset < length;)
  {
    size_t chunk = length - offset;

    if (chunk > 96U)
    {
      chunk = 96U;
    }
    if (!uart_submit(UART_PRI_TEXT, (const uint8_t *)(text + offset),
                     (uint16_t)chunk))
    {
      return false;
    }
    offset += chunk;
  }
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
  uart_pump_tx();
}

void DMA1_Channel2_3_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&s_dma_rx);
  HAL_DMA_IRQHandler(&s_dma_tx);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  (void)Size;
  if ((s_uart != NULL) && (huart == s_uart))
  {
    s_rx_kick = 1U;
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((s_uart != NULL) && (huart == s_uart))
  {
    s_tx_done = true;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((s_uart == NULL) || (huart != s_uart))
  {
    return;
  }
  if (huart->gState != HAL_UART_STATE_BUSY_TX)
  {
    s_tx_busy = false;
    s_tx_done = false;
  }
  __HAL_UART_CLEAR_OREFLAG(huart);
  __HAL_UART_CLEAR_NEFLAG(huart);
  __HAL_UART_CLEAR_FEFLAG(huart);
  __HAL_UART_CLEAR_PEFLAG(huart);
  huart->ErrorCode = HAL_UART_ERROR_NONE;
  uart_start_rx();
}
