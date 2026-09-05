#ifndef FAN_REQUEST_H
#define FAN_REQUEST_H

#include <stdint.h>

/* 0..100 percent duty that G4 should apply to FAN_PWM. */
uint8_t FanRequest_Percent(void);

#endif /* FAN_REQUEST_H */
