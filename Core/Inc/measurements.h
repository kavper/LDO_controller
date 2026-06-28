#ifndef MEASUREMENTS_H
#define MEASUREMENTS_H

#include <stdint.h>

#define MEASUREMENTS_TEMPERATURE_COUNT 4U

typedef struct
{
  /* MCP3464R raw results. */
  int32_t vout_diff_raw;
  int32_t iout_diff_raw;
  int32_t vin_diff_raw;
  int32_t dac_cc_readback_raw;
  int32_t dac_cv_readback_raw;

  /* Engineering values. Hardware-dependent conversion is intentionally TODO. */
  uint32_t vout_mV;
  uint32_t iout_mA;
  uint32_t vin_mV;
  uint32_t dac_cc_readback_mV;
  uint32_t dac_cv_readback_mV;

  uint16_t temperature_raw[MEASUREMENTS_TEMPERATURE_COUNT];
  uint16_t temperature_filtered[MEASUREMENTS_TEMPERATURE_COUNT];
} Measurements_Data_t;

void Measurements_Init(void);
void Measurements_Task(void);
const Measurements_Data_t *Measurements_GetData(void);

#endif /* MEASUREMENTS_H */
