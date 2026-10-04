#include "bleeder_policy.h"

#include <stdio.h>

static int g_failures;

static void Expect(bool got, bool want, const char *msg)
{
  if (got != want)
  {
    g_failures++;
    printf("FAIL: %s (got %u want %u)\n", msg,
           got ? 1U : 0U, want ? 1U : 0U);
  }
}

int main(void)
{
  const uint32_t on_below = 4000U;
  const uint32_t off_above = 4200U;

  Expect(Bleeder_Wanted(false, false, 0U, on_below, off_above), true,
         "output off at 0 V keeps the bleeder on");
  Expect(Bleeder_Wanted(false, false, 2500U, on_below, off_above), true,
         "output off below 4 V keeps the bleeder on");
  Expect(Bleeder_Wanted(false, false, 12000U, on_below, off_above), true,
         "output off above 4 V still bleeds so the rail can fall");

  Expect(Bleeder_Wanted(true, false, 3900U, on_below, off_above), true,
         "output on and Vout below 4 V bleeds");
  Expect(Bleeder_Wanted(true, true, 12000U, on_below, off_above), false,
         "output on and Vout at 12 V releases the bleeder");
  Expect(Bleeder_Wanted(true, true, 4100U, on_below, off_above), true,
         "inside the 4.0..4.2 V band the previous on state is kept");
  Expect(Bleeder_Wanted(true, false, 4100U, on_below, off_above), false,
         "inside the band the previous off state is kept");

  if (g_failures != 0)
  {
    printf("%d failure(s)\n", g_failures);
    return 1;
  }
  printf("test_bleeder_policy: PASS\n");
  return 0;
}
