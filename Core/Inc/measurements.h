#ifndef MEASUREMENTS_H
#define MEASUREMENTS_H

#include <stdint.h>

#define MEASUREMENTS_TEMPERATURE_COUNT 4U

typedef enum
{
  MEASUREMENTS_TEMP_MOSFET = 0U,
  MEASUREMENTS_TEMP_AMBIENT,
  MEASUREMENTS_TEMP_BLEEDER,
  MEASUREMENTS_TEMP_POWER_SUPPLY
} Measurements_TemperatureChannel_t;

typedef struct
{
  /* MCP3464 raw results. */
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

  /*
   * Channel order:
   *   0 - power MOSFET
   *   1 - ambient
   *   2 - bleeder resistor
   *   3 - 3.3 V LDO / 15 V-to-5 V converter area
   */
  uint16_t temperature_raw[MEASUREMENTS_TEMPERATURE_COUNT];
  uint16_t temperature_filtered[MEASUREMENTS_TEMPERATURE_COUNT];
} Measurements_Data_t;

void Measurements_Init(void);
void Measurements_Task(void);
const Measurements_Data_t *Measurements_GetData(void);
int32_t Measurements_McpRawToMicrovolts(int32_t raw);

#endif /* MEASUREMENTS_H */
