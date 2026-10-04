#include "fan_map.h"
#include "ntc_temp.h"

#include <stdio.h>
#include <string.h>

#define PATH_CHANNELS           4U
#define PATH_MOSFET             0U
#define PATH_VREF_MV            3000U
#define PATH_BETA               3435U
#define PATH_MAX_CENTI          6000
#define PATH_CONFIRM_MS         500U
#define PATH_FAILSAFE           40U

static int g_failures;

static void Expect(int cond, const char *msg)
{
    if (!cond) {
        g_failures++;
        printf("FAIL: %s\n", msg);
    }
}

static void ApplyAll(NtcChannel *channel, unsigned index, uint16_t raw)
{
    Ntc_ChannelApply(&channel[index], raw, PATH_VREF_MV, PATH_VREF_MV, PATH_BETA);
}

static bool ReadingsSafe(const NtcChannel *channel)
{
    int32_t centi[PATH_CHANNELS];
    unsigned index;

    for (index = 0U; index < PATH_CHANNELS; index++) {
        centi[index] = channel[index].centi_c;
    }
    return Ntc_ReadingsSafe(centi, PATH_CHANNELS, PATH_MAX_CENTI);
}

static uint8_t FanPercent(const NtcChannel *channel)
{
    bool any = false;
    int32_t hottest = INT32_MIN;
    unsigned index;

    for (index = 0U; index < PATH_CHANNELS; index++) {
        if (channel[index].centi_c == INT32_MIN) {
            continue;
        }
        any = true;
        if (channel[index].centi_c > hottest) {
            hottest = channel[index].centi_c;
        }
    }
    return FanMap_Percent(0U, hottest, any,
                          channel[PATH_MOSFET].centi_c == INT32_MIN,
                          150000U, 2500, 6000, PATH_FAILSAFE);
}

int main(void)
{
    NtcChannel channel[PATH_CHANNELS];
    NtcOutLatch latch;
    bool output_on = true;
    unsigned index;
    unsigned step;

    memset(channel, 0, sizeof(channel));
    memset(&latch, 0, sizeof(latch));
    for (index = 0U; index < PATH_CHANNELS; index++) {
        channel[index].centi_c = INT32_MIN;
    }

    for (index = 0U; index < PATH_CHANNELS; index++) {
        ApplyAll(channel, index, 2048U);
    }
    Expect(channel[PATH_MOSFET].valid, "a midscale sample seeds the filter");
    Expect(channel[PATH_MOSFET].filtered == 2048U, "the first sample is the filter");
    Expect((channel[PATH_MOSFET].centi_c >= 2490) &&
               (channel[PATH_MOSFET].centi_c <= 2510),
           "raw 2048 is 25 C");
    Expect(ReadingsSafe(channel), "four live sensors pass the guard");
    Expect(!Ntc_OutputTick(&output_on, &latch, true, 1000U, PATH_CONFIRM_MS),
           "a good sample leaves the output on");
    Expect(output_on, "the output is still on");
    Expect(FanPercent(channel) == 0U, "25 C and no load leaves the fan stopped");

    ApplyAll(channel, PATH_MOSFET, 4095U);
    Expect(channel[PATH_MOSFET].centi_c == INT32_MIN,
           "an open sensor is a missing measurement");
    Expect(!channel[PATH_MOSFET].valid, "the open sample resets the filter");
    Expect(channel[PATH_MOSFET].filtered == 0U, "the filter does not stay at 4088");
    Expect(!ReadingsSafe(channel), "one open channel fails the guard");
    Expect(FanPercent(channel) == PATH_FAILSAFE,
           "an open MOSFET NTC forces the fan failsafe");
    Expect(!Ntc_OutputTick(&output_on, &latch, false, 2000U, PATH_CONFIRM_MS),
           "the confirm window is still open on the first bad sample");
    Expect(output_on, "the output stays on during the confirm window");
    Expect(!Ntc_OutputTick(&output_on, &latch, false, 2499U, PATH_CONFIRM_MS),
           "499 ms is still inside the confirm window");
    Expect(output_on, "the output is still on at 499 ms");
    Expect(Ntc_OutputTick(&output_on, &latch, false, 2500U, PATH_CONFIRM_MS),
           "500 ms of a missing NTC turns the output off");
    Expect(!output_on, "the output is off after the open sensor");

    for (step = 0U; step < 30U; step++) {
        ApplyAll(channel, PATH_MOSFET, 4095U);
    }
    Expect(channel[PATH_MOSFET].centi_c == INT32_MIN,
           "further open samples stay missing");
    Expect(channel[PATH_MOSFET].filtered == 0U,
           "further open samples do not rebuild the filter");

    ApplyAll(channel, PATH_MOSFET, 2048U);
    ApplyAll(channel, PATH_MOSFET, 0U);
    Expect(channel[PATH_MOSFET].centi_c == INT32_MIN, "a short is missing");
    Expect(!channel[PATH_MOSFET].valid, "a short resets the filter");
    ApplyAll(channel, PATH_MOSFET, 2048U);
    ApplyAll(channel, PATH_MOSFET, NTC_ADC_RAIL_TOLERANCE);
    Expect(channel[PATH_MOSFET].centi_c == INT32_MIN,
           "a short inside the ADC window is missing");
    ApplyAll(channel, PATH_MOSFET, 2048U);
    ApplyAll(channel, PATH_MOSFET, 4088U);
    Expect(channel[PATH_MOSFET].centi_c == INT32_MIN,
           "raw 4088 is rejected before the filter can stall there");
    Expect(channel[PATH_MOSFET].filtered == 0U, "raw 4088 resets the filter");

    ApplyAll(channel, PATH_MOSFET, 2048U);
    Expect(channel[PATH_MOSFET].valid, "reconnecting seeds a new filter");
    Expect(channel[PATH_MOSFET].filtered == 2048U,
           "the reconnected sample replaces the reset filter");
    Expect((channel[PATH_MOSFET].centi_c >= 2490) &&
               (channel[PATH_MOSFET].centi_c <= 2510),
           "the reconnected sensor reads 25 C");
    Expect(ReadingsSafe(channel), "a reconnected sensor passes the guard");
    Expect(!Ntc_OutputTick(&output_on, &latch, true, 3000U, PATH_CONFIRM_MS),
           "a restored sensor does not turn the output back on");
    Expect(!output_on, "the output stays off until a new ON");

    output_on = true;
    Expect(!Ntc_OutputTick(&output_on, &latch, ReadingsSafe(channel), 3000U,
                          PATH_CONFIRM_MS),
           "a new ON is accepted once the sensor reads again");
    Expect(output_on, "the output stays on after the reconnect");
    Expect(FanPercent(channel) == 0U, "a live 25 C sensor releases the fan failsafe");

    if (g_failures != 0) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_ntc_path: PASS\n");
    return 0;
}
