#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include "stm32g0xx_hal.h"
#include <stdint.h>

#define UART_PROTOCOL_SOF1 0xA5U
#define UART_PROTOCOL_SOF2 0x5AU

typedef enum
{
  UART_PROTOCOL_SET_VOLTAGE = 0x01U, /* payload: uint32_t mV */
  UART_PROTOCOL_SET_CURRENT = 0x02U, /* payload: uint32_t mA */
  UART_PROTOCOL_SET_OUTPUT  = 0x03U, /* payload: uint8_t, 0 or 1 */
  UART_PROTOCOL_PING        = 0x04U, /* payload: empty */
  UART_PROTOCOL_TELEMETRY   = 0x80U,
  UART_PROTOCOL_ACK         = 0x81U, /* payload: acknowledged TYPE */
  UART_PROTOCOL_NACK        = 0x82U  /* payload: rejected TYPE, reason */
} UART_ProtocolType_t;

typedef enum
{
  UART_PROTOCOL_NACK_UNKNOWN_TYPE = 1U,
  UART_PROTOCOL_NACK_BAD_PAYLOAD  = 2U
} UART_ProtocolNackReason_t;

/*
 * Frame: A5 5A LEN TYPE SEQ PAYLOAD CRC16_LE
 * LEN counts TYPE + SEQ + PAYLOAD. CRC-16/CCITT-FALSE is calculated over
 * LEN, TYPE, SEQ and PAYLOAD (poly 0x1021, init 0xFFFF).
 * All multibyte payload fields are little-endian.
 *
 * Telemetry payload, in order:
 *   8 x uint32_t: vout_mV, iout_mA, vin_mV, DAC CV readback mV,
 *                 DAC CC readback mV, voltage set mV, current set mA,
 *                 vpre_request_mV
 *   4 x uint8_t:  mode (0 OFF, 1 CV, 2 CC), output, bleeder, pgood
 *   1 x uint32_t: fault flags (currently zero)
 *   4 x uint16_t: temperature raw
 *   4 x uint16_t: temperature IIR-filtered
 */

void UART_Protocol_Init(UART_HandleTypeDef *huart);
void UART_Protocol_Task(void);
void UART_Protocol_QueueTelemetry(void);

#endif /* UART_PROTOCOL_H */
