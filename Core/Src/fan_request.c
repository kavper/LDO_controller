#include "fan_request.h"

#include "app_config.h"
#include "measurements.h"

#include <limits.h>
#include <stdint.h>

static int32_t fan_hottest_centi_C(void)
{
  const Measurements_Data_t *data = Measurements_GetData();
  int32_t hottest = INT32_MIN;
  uint8_t index;

  /* MOSFET, bleeder and local PSU area drive the fan; ambient is advisory. */
  static const uint8_t channels[] =
  {
    MEASUREMENTS_TEMP_MOSFET,
    MEASUREMENTS_TEMP_BLEEDER,
    MEASUREMENTS_TEMP_POWER_SUPPLY
  };

  for (index = 0U; index < (uint8_t)(sizeof(channels) / sizeof(channels[0])); ++index)
  {
    int32_t sample = data->temperature_centi_C[channels[index]];
    if ((sample != INT32_MIN) && (sample > hottest))
    {
      hottest = sample;
    }
  }

  if (data->temperature_centi_C[MEASUREMENTS_TEMP_AMBIENT] != INT32_MIN)
  {
    int32_t ambient = data->temperature_centi_C[MEASUREMENTS_TEMP_AMBIENT] + 500;
    if (ambient > hottest)
    {
      hottest = ambient;
    }
  }

  return hottest;
}

uint8_t FanRequest_Percent(void)
{
  int32_t hottest = fan_hottest_centi_C();
  int32_t span;
  int32_t offset;
  int32_t duty;

  if (hottest == INT32_MIN)
  {
    return FAN_REQUEST_FAILSAFE_PERCENT;
  }
  if (hottest >= FAN_REQUEST_FULL_CENTI_C)
  {
    return 100U;
  }
  if (hottest <= FAN_REQUEST_OFF_CENTI_C)
  {
    return FAN_REQUEST_MIN_PERCENT;
  }

  span = (int32_t)FAN_REQUEST_FULL_CENTI_C - (int32_t)FAN_REQUEST_OFF_CENTI_C;
  offset = hottest - (int32_t)FAN_REQUEST_OFF_CENTI_C;
  duty = (int32_t)FAN_REQUEST_MIN_PERCENT
       + ((offset * (100 - (int32_t)FAN_REQUEST_MIN_PERCENT)) / span);
  if (duty < (int32_t)FAN_REQUEST_MIN_PERCENT)
  {
    return FAN_REQUEST_MIN_PERCENT;
  }
  if (duty > 100)
  {
    return 100U;
  }
  return (uint8_t)duty;
}
