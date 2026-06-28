#include "bleeder.h"

#include "app_config.h"
#include "main.h"
#include "measurements.h"
#include "output_ctrl.h"

#include <stdint.h>

static bool s_enabled;
static uint16_t s_below_threshold_ms;

static void bleeder_set(bool enabled)
{
  s_enabled = enabled;
  HAL_GPIO_WritePin(BLEEDER_EN_GPIO_Port, BLEEDER_EN_Pin,
                    enabled ? BLEEDER_ENABLED_LEVEL : BLEEDER_DISABLED_LEVEL);
}

void Bleeder_Init(void)
{
  s_below_threshold_ms = 0U;
  bleeder_set(false);
}

void Bleeder_Task1ms(void)
{
  uint32_t vout_mV = Measurements_GetData()->vout_mV;

  if (OutputCtrl_IsEnabled())
  {
    s_below_threshold_ms = 0U;
    bleeder_set(false);
    return;
  }

  if (!s_enabled)
  {
    if (vout_mV > BLEEDER_ON_THRESHOLD_MV)
    {
      s_below_threshold_ms = 0U;
      bleeder_set(true);
    }
    return;
  }

  if (vout_mV < BLEEDER_OFF_THRESHOLD_MV)
  {
    if (s_below_threshold_ms < BLEEDER_OFF_CONFIRM_MS)
    {
      ++s_below_threshold_ms;
    }
    if (s_below_threshold_ms >= BLEEDER_OFF_CONFIRM_MS)
    {
      bleeder_set(false);
    }
  }
  else
  {
    s_below_threshold_ms = 0U;
  }
}

bool Bleeder_IsEnabled(void)
{
  return s_enabled;
}
