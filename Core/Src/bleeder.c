#include "bleeder.h"

#include "app_config.h"
#include "bleeder_policy.h"
#include "measurements.h"
#include "output_ctrl.h"

static bool s_enabled;

static void bleeder_set(bool enabled)
{
  s_enabled = enabled;
  /* BLEED_ON is not routed to the G0 MCU on this board revision. */
}

void Bleeder_Init(void)
{
  bleeder_set(false);
}

void Bleeder_Task1ms(void)
{
  uint32_t vout_mV = Measurements_GetData()->vout_mV;
  bool wanted = Bleeder_Wanted(OutputCtrl_IsEnabled(),
                               s_enabled,
                               vout_mV,
                               BLEEDER_RUN_ON_BELOW_MV,
                               BLEEDER_RUN_OFF_ABOVE_MV);

  if (wanted != s_enabled)
  {
    bleeder_set(wanted);
  }
}

bool Bleeder_IsEnabled(void)
{
  return s_enabled;
}
