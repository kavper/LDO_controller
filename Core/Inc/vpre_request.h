#ifndef VPRE_REQUEST_H
#define VPRE_REQUEST_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Preregulator voltage G0 asks G4 for.
 *
 * CV holds Vset + dropout. CC lets Vout fall, so the same headroom would
 * sit across the LDO as (Vset - Vout + dropout) * Iset. Once CC has held,
 * ask for measured Vout + dropout instead.
 *
 * CV follows Vset + margin; confirmed CC follows measured Vout + margin.
 * The minimum is the margin itself, allowing CC down to zero output.
 * Clamp to Vset + margin and the supported preregulator range.
 */
static inline uint32_t Vpre_AddSaturating(uint32_t a, uint32_t b)
{
  if (a > (UINT32_MAX - b))
  {
    return UINT32_MAX;
  }
  return a + b;
}

static inline uint32_t Vpre_RequestMv(bool output_enabled,
                                      bool cc_confirmed,
                                      uint32_t vset_applied_mv,
                                      uint32_t vout_mv,
                                      uint32_t margin_mv,
                                      uint32_t vin_floor_mv,
                                      uint32_t vpre_min_mv,
                                      uint32_t vpre_max_mv)
{
  uint32_t request;
  uint32_t ceiling;

  if (!output_enabled)
  {
    request = vpre_min_mv;
  }
  else if (cc_confirmed)
  {
    request = Vpre_AddSaturating(vout_mv, margin_mv);
    ceiling = Vpre_AddSaturating(vset_applied_mv, margin_mv);
    if (request > ceiling)
    {
      request = ceiling;
    }
    if (request < vin_floor_mv)
    {
      request = vin_floor_mv;
    }
  }
  else
  {
    request = Vpre_AddSaturating(vset_applied_mv, margin_mv);
    if (request < vin_floor_mv)
    {
      request = vin_floor_mv;
    }
  }

  if (request < vpre_min_mv)
  {
    request = vpre_min_mv;
  }
  if (request > vpre_max_mv)
  {
    request = vpre_max_mv;
  }
  return request;
}

#endif /* VPRE_REQUEST_H */
