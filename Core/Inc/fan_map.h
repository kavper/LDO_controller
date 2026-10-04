#ifndef FAN_MAP_H
#define FAN_MAP_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Duty is the higher of two straight lines.
 * Power: 0 W → 0 %, power_full_mw (150 W) → 100 %.
 * Temperature: temp_off_centi (25.00 °C) → 0 %, temp_full_centi (60.00 °C) → 100 %.
 * Centi-degrees are °C×100, the unit stored by the G0 NTCs.
 * A missing temperature uses failsafe_percent on that axis only, so a
 * real power reading can still push the fan harder. A missing MOSFET NTC
 * (critical_missing) does the same even when another sensor is still cold.
 */
static inline uint8_t FanMap_Percent(uint32_t power_mw, int32_t temp_centi_c,
                                     bool temp_valid, bool critical_missing,
                                     uint32_t power_full_mw, int32_t temp_off_centi,
                                     int32_t temp_full_centi, uint8_t failsafe_percent)
{
  uint32_t power_pct = 0U;
  uint32_t temp_pct;

  if (failsafe_percent > 100U)
  {
    failsafe_percent = 100U;
  }

  if ((power_full_mw != 0U) && (power_mw >= power_full_mw))
  {
    power_pct = 100U;
  }
  else if (power_full_mw != 0U)
  {
    power_pct = (uint32_t)(((uint64_t)power_mw * 100ULL + (power_full_mw / 2U))
                           / power_full_mw);
  }
  if (power_pct > 100U)
  {
    power_pct = 100U;
  }

  if (!temp_valid)
  {
    temp_pct = failsafe_percent;
  }
  else if (temp_centi_c <= temp_off_centi)
  {
    temp_pct = 0U;
  }
  else if (temp_centi_c >= temp_full_centi)
  {
    temp_pct = 100U;
  }
  else
  {
    int32_t span = temp_full_centi - temp_off_centi;
    int32_t offset = temp_centi_c - temp_off_centi;

    temp_pct = (uint32_t)((offset * 100 + (span / 2)) / span);
  }
  if (temp_pct > 100U)
  {
    temp_pct = 100U;
  }
  if (critical_missing && (temp_pct < failsafe_percent))
  {
    temp_pct = failsafe_percent;
  }

  return (uint8_t)((power_pct > temp_pct) ? power_pct : temp_pct);
}

#endif /* FAN_MAP_H */
