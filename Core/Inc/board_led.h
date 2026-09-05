#ifndef BOARD_LED_H
#define BOARD_LED_H

#include <stdint.h>

void BoardLed_Init(void);
void BoardLed_Task(uint32_t now);

#endif /* BOARD_LED_H */
