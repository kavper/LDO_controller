#include "control.h"

#include "app_config.h"
#include "dac8562.h"
#include "main.h"
#include "measurements.h"
#include "output_ctrl.h"
#include "vpre_request.h"

#include <limits.h>

static Control_Status_t s_status;
static Control_Mode_t s_filtered_mode;
static GPIO_PinState s_mode_candidate;
static uint8_t s_mode_stable_ms;
static uint8_t s_kill_ms;
static uint16_t s_cc_fold_ms;
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

uint16_t Control_VoltageToDacRaw(uint32_t voltage_mV)
{
  int64_t requested_uV = (int64_t)voltage_mV * 1000LL
                      - DAC_CV_OUTPUT_OFFSET_UV;
  uint64_t corrected_uV;
  uint64_t numerator;
  uint64_t denominator;
  uint64_t code;

  /* DAC cannot command a negative voltage. Zero/OFF must remain code zero. */
  if ((voltage_mV == 0U) || (requested_uV <= 0LL)
      || (DAC_CV_OUTPUT_GAIN_PPM <= 0L))
  {
    return 0U;
  }
  corrected_uV = (uint64_t)((requested_uV * 1000000LL
                         + DAC_CV_OUTPUT_GAIN_PPM / 2LL)
                         / DAC_CV_OUTPUT_GAIN_PPM);
  /* Bound before multiplication, including callers outside the UI range. */
  if (corrected_uV > 36000000ULL)
  {
    corrected_uV = 36000000ULL;
  }
  numerator = corrected_uV * (uint64_t)VOUT_DIFFAMP_FEEDBACK_OHM
            * (uint64_t)UINT16_MAX;
  denominator = (uint64_t)VOUT_DIFFAMP_INPUT_OHM
              * (uint64_t)MCP3464_EXTERNAL_VREF_MV * 1000ULL;
  if (denominator == 0ULL)
  {
    return 0U;
  }
  code = (numerator + denominator / 2ULL) / denominator;
  return (code > UINT16_MAX) ? UINT16_MAX : (uint16_t)code;
}

uint16_t Control_CurrentToDacRaw(uint32_t current_mA)
{
  int64_t requested_uA = (int64_t)current_mA * 1000LL
                      - DAC_CC_OUTPUT_OFFSET_UA;
  uint64_t corrected_uA;
  uint64_t numerator;
  uint64_t denominator;
  uint64_t code;

  if ((current_mA == 0U) || (requested_uA <= 0LL)
      || (DAC_CC_OUTPUT_GAIN_PPM <= 0L))
  {
    return 0U;
  }
  corrected_uA = (uint64_t)((requested_uA * 1000000LL
                         + DAC_CC_OUTPUT_GAIN_PPM / 2LL)
                         / DAC_CC_OUTPUT_GAIN_PPM);
  /* Bound intermediate arithmetic, then saturate instead of wrapping codes. */
  if (corrected_uA > UINT32_MAX)
  {
    corrected_uA = UINT32_MAX;
  }
  numerator = corrected_uA * CURRENT_SENSE_SHUNT_MILLIOHM
            * CURRENT_LIMIT_AMPLIFIER_GAIN * UINT16_MAX;
  denominator = 1000000ULL * MCP3464_EXTERNAL_VREF_MV;
  if (denominator == 0ULL)
  {
    return 0U;
  }
  code = (numerator + denominator / 2ULL) / denominator;
  return (code > UINT16_MAX) ? UINT16_MAX : (uint16_t)code;
}

uint32_t Control_DacRawToMillivolts(uint16_t raw)
{
  return (uint32_t)(((uint64_t)raw * MCP3464_EXTERNAL_VREF_MV
                     + (UINT16_MAX / 2U)) / UINT16_MAX);
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
  uint16_t cv_raw = Control_VoltageToDacRaw(s_status.voltage_applied_mV);
  uint16_t cc_raw = Control_CurrentToDacRaw(s_status.current_applied_mA);
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
  bool cc_confirmed;

  /*
   * Mode is already filtered. Wait a bit longer before folding so a load
   * step that only kisses CC does not start the prereg down. Leaving CC
   * drops the timer at once; G4 then slews VIN back up to Vset + dropout.
   */
  if (s_status.output_enabled && (s_filtered_mode == CONTROL_MODE_CC))
  {
    if (s_cc_fold_ms < VPRE_CC_ENTER_MS)
    {
      ++s_cc_fold_ms;
    }
  }
  else
  {
    s_cc_fold_ms = 0U;
  }

  cc_confirmed = (s_cc_fold_ms >= VPRE_CC_ENTER_MS);
  s_status.vpre_request_mV = Vpre_RequestMv(s_status.output_enabled,
                                            cc_confirmed,
                                            s_status.voltage_applied_mV,
                                            Measurements_GetData()->vout_mV,
                                            VPRE_MARGIN_MV,
                                            VPRE_VIN_FLOOR_MV,
                                            VPRE_MIN_MV,
                                            VPRE_MAX_MV);
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
  s_kill_ms = 0U;
  s_cc_fold_ms = 0U;
  s_last_cv_raw = UINT16_MAX;
  s_last_cc_raw = UINT16_MAX;

  OutputCtrl_Init();
  control_update_dac();
}

void Control_Task1ms(void)
{
  uint32_t voltage_ramp_target;
  uint32_t current_ramp_target;

  if (s_status.output_enabled
      && (HAL_GPIO_ReadPin(POWER_KILL_GPIO_Port, POWER_KILL_Pin)
          == POWER_KILL_ASSERTED_LEVEL))
  {
    if (s_kill_ms < CONTROL_KILL_CONFIRM_MS)
    {
      ++s_kill_ms;
    }
    if (s_kill_ms >= CONTROL_KILL_CONFIRM_MS)
    {
      Control_SetOutputEnabled(false);
    }
  }
  else
  {
    s_kill_ms = 0U;
  }

  voltage_ramp_target = s_status.output_enabled ? s_status.voltage_target_mV : 0U;
  current_ramp_target = s_status.output_enabled ? s_status.current_target_mA : 0U;

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
