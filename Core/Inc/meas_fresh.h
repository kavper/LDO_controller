#ifndef MEAS_FRESH_H
#define MEAS_FRESH_H

#include <stdbool.h>
#include <stdint.h>

/*
 * MCP3464: internal 4.9152 MHz, prescale 1, OSR 256.
 * DMCLK = 1.2288 MHz, one conversion is 208 µs. Five channels and one
 * discarded conversion after every MUX change make a full cycle about
 * 2.1 ms. 50 ms is about twenty-four of those cycles, longer than one
 * 10 ms SPI timeout, and far inside the 500 ms UART stale window.
 * A new telemetry frame does not refresh this age.
 */
#define MEAS_MCP_STALE_MS                50U

/*
 * Four NTCs, one software ADC start per Measurements_Task, beside the
 * 1 ms control tick. 50 ms covers many full rounds and expires a stuck EOC.
 */
#define MEAS_NTC_STALE_MS                50U

#define MEAS_MCP_VOUT                    0U
#define MEAS_MCP_IOUT                    1U
#define MEAS_MCP_VIN                     2U
#define MEAS_MCP_CRITICAL_COUNT          3U
#define MEAS_NTC_COUNT                   4U

static inline bool Meas_StampFresh(uint32_t now_ms, uint32_t stamp_ms,
                                  bool have_stamp, uint32_t limit_ms)
{
    if (!have_stamp) {
        return false;
    }
    return (uint32_t)(now_ms - stamp_ms) <= limit_ms;
}

static inline bool Meas_CriticalFresh(uint32_t now_ms,
                                     const uint32_t *mcp_stamp,
                                     const bool *mcp_have,
                                     const uint32_t *ntc_stamp,
                                     const bool *ntc_have)
{
    uint8_t i;

    if ((mcp_stamp == 0) || (mcp_have == 0) ||
        (ntc_stamp == 0) || (ntc_have == 0)) {
        return false;
    }
    for (i = 0U; i < MEAS_MCP_CRITICAL_COUNT; i++) {
        if (!Meas_StampFresh(now_ms, mcp_stamp[i], mcp_have[i],
                             MEAS_MCP_STALE_MS)) {
            return false;
        }
    }
    for (i = 0U; i < MEAS_NTC_COUNT; i++) {
        if (!Meas_StampFresh(now_ms, ntc_stamp[i], ntc_have[i],
                             MEAS_NTC_STALE_MS)) {
            return false;
        }
    }
    return true;
}

#endif /* MEAS_FRESH_H */
