#include "meas_fresh.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

static void Expect(int cond, const char *msg)
{
    if (!cond) {
        g_failures++;
        printf("FAIL: %s\n", msg);
    }
}

int main(void)
{
    uint32_t mcp_stamp[MEAS_MCP_CRITICAL_COUNT];
    bool mcp_have[MEAS_MCP_CRITICAL_COUNT];
    uint32_t ntc_stamp[MEAS_NTC_COUNT];
    bool ntc_have[MEAS_NTC_COUNT];
    uint8_t i;

    memset(mcp_stamp, 0, sizeof(mcp_stamp));
    memset(ntc_stamp, 0, sizeof(ntc_stamp));
    memset(mcp_have, 0, sizeof(mcp_have));
    memset(ntc_have, 0, sizeof(ntc_have));
    Expect(!Meas_CriticalFresh(1000U, mcp_stamp, mcp_have, ntc_stamp, ntc_have),
           "no conversion yet is not a live sample");
    Expect(!Meas_StampFresh(1000U, 0U, false, MEAS_MCP_STALE_MS),
           "a missing DRDY does not count as fresh");

    for (i = 0U; i < MEAS_MCP_CRITICAL_COUNT; i++) {
        mcp_have[i] = true;
        mcp_stamp[i] = 1000U;
    }
    for (i = 0U; i < MEAS_NTC_COUNT; i++) {
        ntc_have[i] = true;
        ntc_stamp[i] = 1000U;
    }
    Expect(Meas_CriticalFresh(1000U + MEAS_MCP_STALE_MS, mcp_stamp, mcp_have,
                              ntc_stamp, ntc_have),
           "a sample inside the cycle window stays fresh");
    Expect(!Meas_CriticalFresh(1000U + MEAS_MCP_STALE_MS + 1U, mcp_stamp,
                               mcp_have, ntc_stamp, ntc_have),
           "a stopped MCP conversion expires the power channels");
    for (i = 0U; i < MEAS_MCP_CRITICAL_COUNT; i++) {
        mcp_stamp[i] = 1000U + MEAS_MCP_STALE_MS + 1U;
    }
    for (i = 1U; i < MEAS_NTC_COUNT; i++) {
        ntc_stamp[i] = 1000U + MEAS_NTC_STALE_MS + 1U;
    }
    ntc_stamp[0] = 1000U;
    Expect(!Meas_CriticalFresh(1000U + MEAS_NTC_STALE_MS + 1U, mcp_stamp,
                               mcp_have, ntc_stamp, ntc_have),
           "a stuck NTC conversion expires the MOSFET channel");

    if (g_failures != 0) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_meas_fresh: PASS\n");
    return 0;
}
