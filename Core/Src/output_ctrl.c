#include "output_ctrl.h"

#include "app_config.h"
#include "main.h"

static bool s_enabled;

void OutputCtrl_Init(void)
{
  s_enabled = false;
  HAL_GPIO_WritePin(OUT_OFF_GPIO_Port, OUT_OFF_Pin, OUT_OFF_ASSERTED_LEVEL);
}

void OutputCtrl_SetEnabled(bool enabled)
{
  s_enabled = enabled;
  HAL_GPIO_WritePin(OUT_OFF_GPIO_Port, OUT_OFF_Pin,
                    enabled ? OUT_OFF_DEASSERTED_LEVEL : OUT_OFF_ASSERTED_LEVEL);
}

bool OutputCtrl_IsEnabled(void)
{
  return s_enabled;
}
