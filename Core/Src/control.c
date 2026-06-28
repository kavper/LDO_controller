#include "control.h"

#include "app_config.h"
#include "dac8562.h"
#include "main.h"
#include "measurements.h"
#include "output_ctrl.h"

#include <limits.h>

static Control_Status_t s_status;
static Control_Mode_t s_filtered_mode;
static GPIO_PinState s_mode_candidate;
static uint8_t s_mode_stable_ms;
static uint16_t s_last_cv_raw;
static uint16_t s_last_cc_raw;

static uint32_t control_clamp_u32(uint32_t value, uint32_t minimum, uint32_t maximum)
{
  if (value < minimum)
  {
    return minimum;
  }
  if (value > maximum)
  {
    return maximum;
  }
  return value;
}

static uint32_t control_ramp(uint32_t actual, uint32_t target, uint32_t step)
{
  if (actual < target)
  {
    uint32_t remaining = target - actual;
    return actual + ((remaining < step) ? remaining : step);
  }
  if (actual > target)
  {
    uint32_t remaining = actual - target;
    return actual - ((remaining < step) ? remaining : step);
  }
  return actual;
}

static uint16_t control_voltage_to_dac_raw(uint32_t voltage_mV)
{
  /*
   * TODO: replace with calibrated CV analog-path scaling. Returning zero is
   * intentionally fail-safe until the resistor network and amplifier gain are known.
   */
  (void)voltage_mV;
  return 0U;
}

static uint16_t control_current_to_dac_raw(uint32_t current_mA)
{
  /* TODO: replace with calibrated CC shunt/amplifier-path scaling. */
  (void)current_mA;
  return 0U;
}

static void control_update_mode(void)
{
  GPIO_PinState sample = HAL_GPIO_ReadPin(CC_CV_STATE_GPIO_Port, CC_CV_STATE_Pin);

  if (sample == s_mode_candidate)
  {
    if (s_mode_stable_ms < CONTROL_MODE_FILTER_MS)
    {
      ++s_mode_stable_ms;
    }
  }
  else
  {
    s_mode_candidate = sample;
    s_mode_stable_ms = 1U;
  }

  if (s_mode_stable_ms >= CONTROL_MODE_FILTER_MS)
  {
    s_filtered_mode = (sample == CC_CV_STATE_CC_LEVEL) ? CONTROL_MODE_CC : CONTROL_MODE_CV;
  }

  s_status.mode = s_status.output_enabled ? s_filtered_mode : CONTROL_MODE_OFF;
}

static void control_update_dac(void)
{
  uint16_t cv_raw = control_voltage_to_dac_raw(s_status.voltage_applied_mV);
  uint16_t cc_raw = control_current_to_dac_raw(s_status.current_applied_mA);
  bool update = false;

  if (cv_raw != s_last_cv_raw)
  {
    if (DAC8562_SetCVRaw(cv_raw) == HAL_OK)
    {
      s_last_cv_raw = cv_raw;
      update = true;
    }
  }
  if (cc_raw != s_last_cc_raw)
  {
    if (DAC8562_SetCCRaw(cc_raw) == HAL_OK)
    {
      s_last_cc_raw = cc_raw;
      update = true;
    }
  }
  if (update)
  {
    DAC8562_LdacPulse();
  }
}

static void control_update_vpre_request(void)
{
  uint32_t request;

  if (!s_status.output_enabled)
  {
    request = VPRE_MIN_MV;
  }
  else if (s_status.mode == CONTROL_MODE_CC)
  {
    request = Measurements_GetData()->vout_mV + VPRE_MARGIN_MV;
  }
  else
  {
    request = s_status.voltage_applied_mV + VPRE_MARGIN_MV;
  }

  s_status.vpre_request_mV = control_clamp_u32(request, VPRE_MIN_MV, VPRE_MAX_MV);

  /*
   * The STM32G4 preregulator should apply its own slew rate and hysteresis,
   * for example about -0.3 V / +1.0 V relative to vpre_request_mV.
   */
}

void Control_Init(void)
{
  s_status.voltage_target_mV = 0U;
  s_status.current_target_mA = 0U;
  s_status.voltage_applied_mV = 0U;
  s_status.current_applied_mA = 0U;
  s_status.vpre_request_mV = VPRE_MIN_MV;
  s_status.mode = CONTROL_MODE_OFF;
  s_status.output_enabled = false;

  s_filtered_mode = CONTROL_MODE_CV;
  s_mode_candidate = HAL_GPIO_ReadPin(CC_CV_STATE_GPIO_Port, CC_CV_STATE_Pin);
  s_mode_stable_ms = 0U;
  s_last_cv_raw = UINT16_MAX;
  s_last_cc_raw = UINT16_MAX;

  OutputCtrl_Init();
  control_update_dac();
}

void Control_Task1ms(void)
{
  uint32_t voltage_ramp_target = s_status.output_enabled ? s_status.voltage_target_mV : 0U;
  uint32_t current_ramp_target = s_status.output_enabled ? s_status.current_target_mA : 0U;

  s_status.voltage_applied_mV = control_ramp(s_status.voltage_applied_mV,
                                             voltage_ramp_target,
                                             CONTROL_VOLTAGE_RAMP_MV_PER_MS);
  s_status.current_applied_mA = control_ramp(s_status.current_applied_mA,
                                             current_ramp_target,
                                             CONTROL_CURRENT_RAMP_MA_PER_MS);
  control_update_mode();
  control_update_dac();
  control_update_vpre_request();
}

void Control_SetVoltageTarget(uint32_t voltage_mV)
{
  s_status.voltage_target_mV = control_clamp_u32(voltage_mV,
                                                 APP_VOLTAGE_MIN_MV,
                                                 APP_VOLTAGE_MAX_MV);
}

void Control_SetCurrentTarget(uint32_t current_mA)
{
  s_status.current_target_mA = control_clamp_u32(current_mA,
                                                 APP_CURRENT_MIN_MA,
                                                 APP_CURRENT_MAX_MA);
}

void Control_SetOutputEnabled(bool enabled)
{
  OutputCtrl_SetEnabled(enabled);
  s_status.output_enabled = enabled;
  if (!enabled)
  {
    s_status.mode = CONTROL_MODE_OFF;
  }
}

const Control_Status_t *Control_GetStatus(void)
{
  return &s_status;
}
