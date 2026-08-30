#include "bleeder.h"

#include "app_config.h"
#include "control.h"
#include "main.h"
#include "measurements.h"
#include "output_ctrl.h"

#include <stdint.h>

static bool s_enabled;
static uint16_t s_below_threshold_ms;

static void bleeder_set(bool enabled)
{
  s_enabled = enabled;
  /* BLEED_ON is not routed to the G0 MCU on this board revision. */
}

void Bleeder_Init(void)
{
  s_below_threshold_ms = 0U;
  bleeder_set(false);
}

void Bleeder_Task1ms(void)
{
  const Control_Status_t *control;
  uint32_t vout_mV = Measurements_GetData()->vout_mV;
  uint32_t set_mV;

  if (OutputCtrl_IsEnabled())
  {
    s_below_threshold_ms = 0U;
    control = Control_GetStatus();
    set_mV = control->voltage_target_mV;

    if (s_enabled)
    {
      if (set_mV >= BLEEDER_RUN_OFF_ABOVE_MV)
      {
        bleeder_set(false);
      }
    }
    else if (set_mV < BLEEDER_RUN_ON_BELOW_MV)
    {
      bleeder_set(true);
    }
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
