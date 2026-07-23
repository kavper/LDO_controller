#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

void UART_Console_Init(bool mcp_ok, bool dac_ok);
void UART_Console_Task(uint32_t now);
void UART_Console_QueueStatus(void);

#endif /* UART_CONSOLE_H */
