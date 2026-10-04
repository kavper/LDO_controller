#include "fan_map.h"

#include <stdio.h>

static int g_failures;

static void ExpectEq(unsigned got, unsigned want, const char *msg)
{
  if (got != want)
  {
    g_failures++;
    printf("FAIL: %s (got %u want %u)\n", msg, got, want);
  }
}

int main(void)
{
  const uint32_t full_mw = 150000U;
  const int32_t off_c = 2500;
  const int32_t full_c = 6000;
  const uint8_t failsafe = 40U;

  ExpectEq(FanMap_Percent(0U, 2500, true, false, full_mw, off_c, full_c, failsafe), 0U,
           "0 W and 25.00 C is stopped");
  ExpectEq(FanMap_Percent(0U, 2000, true, false, full_mw, off_c, full_c, failsafe), 0U,
           "colder than 25 C stays at 0");
  ExpectEq(FanMap_Percent(0U, 6000, true, false, full_mw, off_c, full_c, failsafe), 100U,
           "60.00 C is full speed");
  ExpectEq(FanMap_Percent(0U, 8000, true, false, full_mw, off_c, full_c, failsafe), 100U,
           "hotter than 60 C stays at full speed");
  ExpectEq(FanMap_Percent(0U, 4250, true, false, full_mw, off_c, full_c, failsafe), 50U,
           "42.50 C is the middle of the temperature line");
  ExpectEq(FanMap_Percent(0U, 3000, true, false, full_mw, off_c, full_c, failsafe), 14U,
           "30.00 C is 14 percent");

  ExpectEq(FanMap_Percent(150000U, 2500, true, false, full_mw, off_c, full_c, failsafe),
           100U, "150 W at 25 C is full speed");
  ExpectEq(FanMap_Percent(200000U, 2500, true, false, full_mw, off_c, full_c, failsafe),
           100U, "above 150 W stays at full speed");
  ExpectEq(FanMap_Percent(75000U, 2500, true, false, full_mw, off_c, full_c, failsafe),
           50U, "75 W is the middle of the power line");
  ExpectEq(FanMap_Percent(100000U, 2500, true, false, full_mw, off_c, full_c, failsafe),
           67U, "100 W is 67 percent");

  ExpectEq(FanMap_Percent(100000U, 3000, true, false, full_mw, off_c, full_c, failsafe),
           67U, "power wins when it asks for more than temperature");
  ExpectEq(FanMap_Percent(10000U, 5000, true, false, full_mw, off_c, full_c, failsafe),
           71U, "temperature wins when it asks for more than power");

  ExpectEq(FanMap_Percent(0U, 0, false, false, full_mw, off_c, full_c, failsafe), 40U,
           "no NTC and no load uses the 40 percent failsafe");
  ExpectEq(FanMap_Percent(150000U, 0, false, false, full_mw, off_c, full_c, failsafe),
           100U, "no NTC still follows a full power request");
  ExpectEq(FanMap_Percent(30000U, 0, false, false, full_mw, off_c, full_c, failsafe),
           40U, "a modest load does not undercut the temperature failsafe");

  ExpectEq(FanMap_Percent(0U, 2500, true, true, full_mw, off_c, full_c, failsafe),
           40U, "a dead MOSFET NTC keeps at least the failsafe");
  ExpectEq(FanMap_Percent(0U, 6000, true, true, full_mw, off_c, full_c, failsafe),
           100U, "a dead MOSFET NTC still follows a hotter remaining sensor");
  ExpectEq(FanMap_Percent(150000U, 2500, true, true, full_mw, off_c, full_c, failsafe),
           100U, "a dead MOSFET NTC still follows full power");

  if (g_failures != 0)
  {
    printf("%d failure(s)\n", g_failures);
    return 1;
  }
  printf("test_fan_map: PASS\n");
  return 0;
}
