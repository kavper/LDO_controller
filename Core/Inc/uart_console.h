#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

void UART_Console_Init(bool mcp_ok, bool dac_ok);
void UART_Console_Task(uint32_t now);
void UART_Console_QueueMachineTelemetry(void);
const char *UART_Console_GetFault(void);
bool UART_Console_ApplySetpoint(uint32_t voltage_mV, uint32_t current_mA);
const char *UART_Console_SetOutput(bool enabled);

#endif /* UART_CONSOLE_H */
