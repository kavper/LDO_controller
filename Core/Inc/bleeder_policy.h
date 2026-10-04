#ifndef BLEEDER_POLICY_H
#define BLEEDER_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The bleeder follows measured Vout, not the setpoint.
 * Below 4.000 V it is on whether or not the output is enabled, so a
 * low-voltage run still has its minimum load after the supply is turned
 * off. While the output is off the resistor stays on at any voltage,
 * which is what discharges a rail that is still above 4 V.
 * Between 4.000 V and 4.200 V the previous state is kept.
 */
static inline bool Bleeder_Wanted(bool output_on, bool bleed_was_on,
                                  uint32_t vout_mv, uint32_t on_below_mv,
                                  uint32_t off_above_mv)
{
  if (!output_on)
  {
    return true;
  }
  if (vout_mv < on_below_mv)
  {
    return true;
  }
  if (vout_mv >= off_above_mv)
  {
    return false;
  }
  return bleed_was_on;
}

#endif /* BLEEDER_POLICY_H */
