#ifndef NTC_TEMP_H
#define NTC_TEMP_H

#include <math.h>
#include <stdint.h>

/*
 * 10 kΩ NTC to ground, 10 kΩ pull-up to 3V_REFR. ADC full scale is VREF+.
 * On this board VREF+ and the pull-up are the same 3.000 V rail, so the
 * two arguments are equal. raw 0 is a short. A reading at the rail is an
 * open sensor. Both come back as INT32_MIN, which the wire sends as missing.
 */
static inline int32_t Ntc_CentiC(uint16_t raw, uint32_t vref_mv,
                                uint32_t vdiv_mv, uint32_t beta_k)
{
    const float adc_full_scale = 4095.0f;
    const float nominal_k = 298.15f;
    float ntc_mv;
    float ratio;
    float temperature_k;
    float temperature_c;

    if ((raw == 0U) || (vref_mv == 0U) || (vdiv_mv == 0U) || (beta_k == 0U)) {
        return INT32_MIN;
    }

    ntc_mv = ((float)raw * (float)vref_mv) / adc_full_scale;
    if (ntc_mv >= (float)vdiv_mv) {
        return INT32_MIN;
    }

    ratio = ntc_mv / ((float)vdiv_mv - ntc_mv);
    temperature_k = 1.0f / ((1.0f / nominal_k) + (logf(ratio) / (float)beta_k));
    temperature_c = temperature_k - 273.15f;
    return (int32_t)((temperature_c >= 0.0f) ? (temperature_c * 100.0f + 0.5f)
                                             : (temperature_c * 100.0f - 0.5f));
}

#endif /* NTC_TEMP_H */
