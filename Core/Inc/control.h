#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  CONTROL_MODE_OFF = 0U,
  CONTROL_MODE_CV  = 1U,
  CONTROL_MODE_CC  = 2U
} Control_Mode_t;

typedef struct
{
  uint32_t voltage_target_mV;
  uint32_t current_target_mA;
  uint32_t voltage_applied_mV;
  uint32_t current_applied_mA;
  uint32_t vpre_request_mV;
  Control_Mode_t mode;
  bool output_enabled;
} Control_Status_t;

void Control_Init(void);
void Control_Task1ms(void);
void Control_SetVoltageTarget(uint32_t voltage_mV);
void Control_SetCurrentTarget(uint32_t current_mA);
void Control_SetOutputEnabled(bool enabled);
const Control_Status_t *Control_GetStatus(void);

#endif /* CONTROL_H */
