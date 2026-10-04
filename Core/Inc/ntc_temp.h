#ifndef NTC_TEMP_H
#define NTC_TEMP_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * 10 kΩ NTC to ground, 10 kΩ pull-up to 3V_REFR. ADC full scale is VREF+.
 * On this board VREF+ and the pull-up are the same 3.000 V rail.
 *
 * A short sits on ground and an open sits on the pull-up. The STM32G0
 * 12-bit ADC is a few LSB off at the rails (offset, INL, noise, and the
 * 1.5-cycle sample), so the fault window is 16 counts, about 12 mV.
 * A live 10 kΩ / 3435 K sensor is still ~160 counts below full scale at
 * −40 °C and ~130 counts above zero at 150 °C.
 *
 * The IIR divides by 8, so a raw code of 4095 walks the filter to 4088
 * and then stops. That leftover is about −81 °C and must never be stored.
 * Reject the raw sample before the filter sees it.
 */
#define NTC_ADC_FULL_SCALE               4095U
#define NTC_ADC_RAIL_TOLERANCE           16U

typedef struct {
    uint16_t filtered;
    bool valid;
    int32_t centi_c;
} NtcChannel;

typedef struct {
    bool pending;
    uint32_t since_ms;
} NtcOutLatch;

static inline bool Ntc_RawAtRail(uint16_t raw)
{
    return (raw <= NTC_ADC_RAIL_TOLERANCE) ||
           (raw >= (NTC_ADC_FULL_SCALE - NTC_ADC_RAIL_TOLERANCE));
}

static inline void Ntc_ChannelReset(NtcChannel *channel)
{
    if (channel == 0) {
        return;
    }
    channel->filtered = 0U;
    channel->valid = false;
    channel->centi_c = INT32_MIN;
}

static inline int32_t Ntc_CentiC(uint16_t raw, uint32_t vref_mv,
                                uint32_t vdiv_mv, uint32_t beta_k)
{
    const float adc_full_scale = 4095.0f;
    const float nominal_k = 298.15f;
    float ntc_mv;
    float ratio;
    float temperature_k;
    float temperature_c;

    if (Ntc_RawAtRail(raw) || (vref_mv == 0U) || (vdiv_mv == 0U) ||
        (beta_k == 0U)) {
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

/* Rail codes reset the channel. Any other raw code updates the IIR first. */
static inline void Ntc_ChannelApply(NtcChannel *channel, uint16_t raw,
                                   uint32_t vref_mv, uint32_t vdiv_mv,
                                   uint32_t beta_k)
{
    int32_t filtered;

    if (channel == 0) {
        return;
    }
    if (Ntc_RawAtRail(raw)) {
        Ntc_ChannelReset(channel);
        return;
    }
    if (!channel->valid) {
        channel->filtered = raw;
        channel->valid = true;
    } else {
        filtered = (int32_t)channel->filtered;
        filtered += ((int32_t)raw - filtered) / 8;
        channel->filtered = (uint16_t)filtered;
    }
    channel->centi_c = Ntc_CentiC(channel->filtered, vref_mv, vdiv_mv, beta_k);
}

static inline bool Ntc_ReadingsSafe(const int32_t *centi_c, unsigned count,
                                   int32_t max_centi_c)
{
    unsigned index;

    if (centi_c == 0) {
        return false;
    }
    for (index = 0U; index < count; index++) {
        if ((centi_c[index] == INT32_MIN) || (centi_c[index] >= max_centi_c)) {
            return false;
        }
    }
    return true;
}

/*
 * Temperature branch of the runtime guard. confirm_ms is
 * CONSOLE_TEMPERATURE_CONFIRM_MS (500). A safe sample clears the window.
 * Turning the output off does not turn it back on when the sensor returns.
 */
static inline bool Ntc_OutputTick(bool *output_on, NtcOutLatch *latch,
                                 bool readings_safe, uint32_t now_ms,
                                 uint32_t confirm_ms)
{
    if ((output_on == 0) || (latch == 0) || !*output_on) {
        return false;
    }
    if (readings_safe) {
        latch->pending = false;
        return false;
    }
    if (!latch->pending) {
        latch->pending = true;
        latch->since_ms = now_ms;
        if (confirm_ms != 0U) {
            return false;
        }
    }
    if ((uint32_t)(now_ms - latch->since_ms) >= confirm_ms) {
        *output_on = false;
        latch->pending = false;
        return true;
    }
    return false;
}

#endif /* NTC_TEMP_H */
