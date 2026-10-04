#include "fan_request.h"

#include "app_config.h"
#include "fan_map.h"
#include "measurements.h"

#include <limits.h>
#include <stdint.h>

static int32_t fan_hottest_centi_C(bool *valid)
{
  const Measurements_Data_t *data = Measurements_GetData();
  int32_t hottest = INT32_MIN;
  uint8_t index;

  *valid = false;
  for (index = 0U; index < MEASUREMENTS_TEMPERATURE_COUNT; ++index)
  {
    int32_t sample = data->temperature_centi_C[index];

    if (sample == INT32_MIN)
    {
      continue;
    }
    *valid = true;
    if (sample > hottest)
    {
      hottest = sample;
    }
  }
  return hottest;
}

static uint32_t fan_power_mw(void)
{
  const Measurements_Data_t *data = Measurements_GetData();

  /* mV * mA = µW. 150 W is 150000 mW. */
  return (uint32_t)(((uint64_t)data->vout_mV * (uint64_t)data->iout_mA) / 1000ULL);
}

uint8_t FanRequest_Percent(void)
{
  const Measurements_Data_t *data = Measurements_GetData();
  bool temp_valid = false;
  bool mosfet_missing;
  int32_t hottest = fan_hottest_centi_C(&temp_valid);

  mosfet_missing =
      (data->temperature_centi_C[MEASUREMENTS_TEMP_MOSFET] == INT32_MIN);
  return FanMap_Percent(fan_power_mw(), hottest, temp_valid, mosfet_missing,
                        FAN_MAP_POWER_FULL_MW,
                        FAN_MAP_TEMP_OFF_CENTI_C,
                        FAN_MAP_TEMP_FULL_CENTI_C,
                        FAN_REQUEST_FAILSAFE_PERCENT);
}
