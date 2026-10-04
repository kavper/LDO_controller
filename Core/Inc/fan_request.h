#ifndef FAN_REQUEST_H
#define FAN_REQUEST_H

#include <stdint.h>

/* 0..100 percent. Higher of the 0..150 W and 25..60 °C maps. */
uint8_t FanRequest_Percent(void);

#endif /* FAN_REQUEST_H */
