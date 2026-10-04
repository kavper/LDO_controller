#include "ntc_temp.h"

#include <stdio.h>
#include <stdint.h>

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
    int32_t t25;
    int32_t t60;

    t25 = Ntc_CentiC(2048U, 3000U, 3000U, 3435U);
    t60 = Ntc_CentiC(940U, 3000U, 3000U, 3435U);
    Expect((t25 >= 2490) && (t25 <= 2510), "midscale counts are 25 C");
    Expect((t60 >= 5980) && (t60 <= 6020), "940 counts are 60 C");
    Expect(Ntc_CentiC(0U, 3000U, 3000U, 3435U) == INT32_MIN,
           "a shorted NTC is missing");
    Expect(Ntc_CentiC(4095U, 3000U, 3000U, 3435U) == INT32_MIN,
           "an open NTC sits on the rail and is missing");

    if (g_failures != 0) {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("test_ntc_temp: PASS (%ld and %ld centi-C)\n", (long)t25, (long)t60);
    return 0;
}
