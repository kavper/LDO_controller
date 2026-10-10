#include "vpre_request.h"

#include <stdio.h>
#include <stdint.h>

static int g_failures;

static void ExpectEq(uint32_t got, uint32_t want, const char *msg)
{
  if (got != want)
  {
    g_failures++;
    printf("FAIL: %s (got %lu want %lu)\n", msg,
           (unsigned long)got, (unsigned long)want);
  }
}

int main(void)
{
  const uint32_t margin = 1500U;
  const uint32_t floor = 1500U;
  const uint32_t vmin = 1500U;
  const uint32_t vmax = 36000U;

  ExpectEq(Vpre_RequestMv(false, false, 12000U, 12000U, margin, floor, vmin, vmax),
           vmin, "output off asks for the minimum");

  ExpectEq(Vpre_RequestMv(true, false, 12000U, 8000U, margin, floor, vmin, vmax),
           13500U, "CV keeps Vset + dropout even while Vout is low");

  ExpectEq(Vpre_RequestMv(true, false, 4000U, 4000U, margin, floor, vmin, vmax),
           5500U, "4 V CV requests 5.5 V");

  ExpectEq(Vpre_RequestMv(true, true, 12000U, 8000U, margin, floor, vmin, vmax),
           9500U, "confirmed CC asks for Vout + dropout");

  ExpectEq(Vpre_RequestMv(true, true, 12000U, 1000U, margin, floor, vmin, vmax),
           2500U, "1 V CC requests 2.5 V");

  ExpectEq(Vpre_RequestMv(true, true, 12000U, 13000U, margin, floor, vmin, vmax),
           13500U, "CC never asks above Vset + dropout");

  ExpectEq(Vpre_RequestMv(true, true, 4000U, 2000U, margin, floor, vmin, vmax),
           3500U, "2 V CC requests 3.5 V");

  ExpectEq(Vpre_RequestMv(true, true, 35000U, 35000U, margin, floor, vmin, vmax),
           vmax, "request clamps to the preregulator maximum");

  ExpectEq(Vpre_RequestMv(true, false, 1000U, 1000U, margin, floor, vmin, vmax),
           2500U, "1 V CV requests exactly 2.5 V");
  ExpectEq(Vpre_RequestMv(true, true, 12000U, 0U, margin, floor, vmin, vmax),
           1500U, "short-circuit CC requests 1.5 V");

  if (g_failures != 0)
  {
    printf("%d failure(s)\n", g_failures);
    return 1;
  }
  printf("test_vpre_request: PASS\n");
  return 0;
}
