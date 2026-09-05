#include "uart_console.h"

#include "app_config.h"
#include "bleeder.h"
#include "control.h"
#include "fan_request.h"
#include "main.h"
#include "measurements.h"
#include "uart_protocol.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define CONSOLE_LINE_SIZE 96U

static bool s_mcp_ok;
static bool s_dac_ok;
static const char *s_fault;
static const char *s_fault_candidate;
static uint32_t s_fault_candidate_since;

static void console_skip_spaces(const char **cursor)
{
  while (**cursor == ' ')
  {
    ++(*cursor);
  }
}

static bool console_take_token(const char **cursor, const char *token)
{
  size_t length = strlen(token);

  if (strncmp(*cursor, token, length) != 0)
  {
    return false;
  }
  *cursor += length;
  return true;
}

static bool console_parse_milli(const char **cursor, uint32_t *value)
{
  uint32_t whole = 0U;
  uint32_t fraction = 0U;
  uint8_t fraction_digits = 0U;
  bool have_digit = false;

  while (isdigit((unsigned char)**cursor) != 0)
  {
    have_digit = true;
    if (whole > 100000U)
    {
      return false;
    }
    whole = whole * 10U + (uint32_t)(**cursor - '0');
    ++(*cursor);
  }
  if (**cursor == '.')
  {
    ++(*cursor);
    while (isdigit((unsigned char)**cursor) != 0)
    {
      if (fraction_digits >= 3U)
      {
        return false;
      }
      fraction = fraction * 10U + (uint32_t)(**cursor - '0');
      ++fraction_digits;
      ++(*cursor);
    }
  }
  if (!have_digit)
  {
    return false;
  }
  while (fraction_digits < 3U)
  {
    fraction *= 10U;
    ++fraction_digits;
  }
  *value = whole * 1000U + fraction;
  return true;
}

static bool console_temperatures_safe(const Measurements_Data_t *data)
{
  uint8_t index;

  for (index = 0U; index < MEASUREMENTS_TEMPERATURE_COUNT; ++index)
  {
    if ((data->temperature_centi_C[index] == INT32_MIN)
        || (data->temperature_centi_C[index]
            >= CONSOLE_MAXIMUM_TEMPERATURE_CENTI_C))
    {
      return false;
    }
  }
  return true;
}

static const char *console_preflight_fault(void)
{
  const Measurements_Data_t *data = Measurements_GetData();

  if (!s_mcp_ok || !s_dac_ok)
  {
    return "ADC_OR_DAC_INIT";
  }
  if (HAL_GPIO_ReadPin(PGOOD_5V_IN_GPIO_Port, PGOOD_5V_IN_Pin)
      != PGOOD_ASSERTED_LEVEL)
  {
    return "PGOOD_5V";
  }
  if (HAL_GPIO_ReadPin(POWER_KILL_GPIO_Port, POWER_KILL_Pin)
      == POWER_KILL_ASSERTED_LEVEL)
  {
    return "POWER_KILL";
  }
  if (data->vin_mV < CONSOLE_MINIMUM_VIN_MV)
  {
    return "VIN_LOW";
  }
  if (data->vout_mV > 250U)
  {
    return "VOUT_NOT_ZERO";
  }
  if (!console_temperatures_safe(data))
  {
    return "TEMPERATURE";
  }
  return NULL;
}

bool UART_Console_ApplySetpoint(uint32_t voltage_mV, uint32_t current_mA)
{
  if ((voltage_mV > APP_VOLTAGE_MAX_MV)
      || (current_mA > APP_CURRENT_MAX_MA))
  {
    return false;
  }

  Control_SetVoltageTarget(voltage_mV);
  Control_SetCurrentTarget(current_mA);
  s_fault_candidate = NULL;
  return true;
}

const char *UART_Console_SetOutput(bool enabled)
{
  const char *fault;

  if (!enabled)
  {
    Control_SetOutputEnabled(false);
    s_fault = "NONE";
    s_fault_candidate = NULL;
    return NULL;
  }
  if (Control_GetStatus()->output_enabled)
  {
    return NULL;
  }

  fault = console_preflight_fault();
  if (fault != NULL)
  {
    return fault;
  }

  s_fault = "NONE";
  s_fault_candidate = NULL;
  Control_SetOutputEnabled(true);
  return NULL;
}

static uint32_t console_vout_limit_mV(uint32_t target_mV,
                                      uint32_t minimum_margin_mV,
                                      uint32_t margin_percent)
{
  uint32_t margin = target_mV * margin_percent / 100U;

  if (margin < minimum_margin_mV)
  {
    margin = minimum_margin_mV;
  }
  return target_mV + margin;
}

static const char *console_runtime_fault_condition(uint32_t now,
                                                   uint32_t *confirm_ms)
{
  const Measurements_Data_t *data = Measurements_GetData();
  const Control_Status_t *control = Control_GetStatus();
  uint32_t voltage_protection_reference_mV;

  if (!s_mcp_ok || !s_dac_ok)
  {
    *confirm_ms = 0U;
    return "HW_INIT";
  }
  if (HAL_GPIO_ReadPin(PGOOD_5V_IN_GPIO_Port, PGOOD_5V_IN_Pin)
      != PGOOD_ASSERTED_LEVEL)
  {
    *confirm_ms = CONSOLE_PGOOD_CONFIRM_MS;
    return "PGOOD_LOST";
  }
  if (HAL_GPIO_ReadPin(POWER_KILL_GPIO_Port, POWER_KILL_Pin)
      == POWER_KILL_ASSERTED_LEVEL)
  {
    *confirm_ms = CONSOLE_PGOOD_CONFIRM_MS;
    return "POWER_KILL";
  }
  if (data->vin_mV < CONSOLE_MINIMUM_VIN_MV)
  {
    *confirm_ms = CONSOLE_VIN_CONFIRM_MS;
    return "VIN_LOW";
  }

  voltage_protection_reference_mV = control->voltage_target_mV;
  if (control->voltage_applied_mV > voltage_protection_reference_mV)
  {
    voltage_protection_reference_mV = control->voltage_applied_mV;
  }
  if (data->vout_mV
      > console_vout_limit_mV(voltage_protection_reference_mV,
                              CONSOLE_VOUT_HARD_OVERSHOOT_MIN_MV,
                              CONSOLE_VOUT_HARD_OVERSHOOT_PERCENT))
  {
    *confirm_ms = CONSOLE_VOUT_HARD_OV_CONFIRM_MS;
    return "VOUT_HARD";
  }
  if (data->vout_mV
      > console_vout_limit_mV(voltage_protection_reference_mV,
                              CONSOLE_VOUT_OVERSHOOT_MIN_MV,
                              CONSOLE_VOUT_OVERSHOOT_PERCENT))
  {
    *confirm_ms = CONSOLE_VOUT_OV_CONFIRM_MS;
    return "VOUT_HIGH";
  }
  /*
   * G0 owns the final-output current measurement and analogue CC loop.
   * Reaching 5.5 A means that normal regulation failed: open OUT_OFF locally,
   * then report a latched fault so G4 also removes DCDC power permit.
   */
  if (data->iout_mA >= CONSOLE_IOUT_EMERGENCY_MA)
  {
    *confirm_ms = CONSOLE_IOUT_EMERGENCY_CONFIRM_MS;
    return "IOUT_HARD";
  }
  if (!console_temperatures_safe(data))
  {
    *confirm_ms = CONSOLE_TEMPERATURE_CONFIRM_MS;
    return "TEMP_HIGH";
  }
  (void)now;
  /*
   * Analog CC/CV loops hold Iset and Vset. DAC readback and STM_CC_CV are
   * telemetry only — never force the output off because the supply limited.
   */
  return NULL;
}

static const char *console_runtime_fault(uint32_t now)
{
  uint32_t confirm_ms = 0U;
  const char *condition = console_runtime_fault_condition(now, &confirm_ms);

  if (condition == NULL)
  {
    s_fault_candidate = NULL;
    return NULL;
  }
  if ((s_fault_candidate == NULL)
      || (strcmp(s_fault_candidate, condition) != 0))
  {
    s_fault_candidate = condition;
    s_fault_candidate_since = now;
    return (confirm_ms == 0U) ? condition : NULL;
  }
  if ((uint32_t)(now - s_fault_candidate_since) >= confirm_ms)
  {
    return condition;
  }
  return NULL;
}

void UART_Console_QueueMachineTelemetry(void)
{
  const Measurements_Data_t *data = Measurements_GetData();
  const Control_Status_t *control = Control_GetStatus();
  char line[220];
  int written;

  written = snprintf(
      line, sizeof(line),
      "TLM out=%u mode=%u vset=%lu vout=%lu iset=%lu iout=%lu vin=%lu "
      "t1=%ld t2=%ld t3=%ld t4=%ld bleed=%u fan=%u pgood=%u kill=%u "
      "outoff=%u cccv=%u fault=%s\r\n",
      control->output_enabled ? 1U : 0U,
      (unsigned int)control->mode,
      (unsigned long)control->voltage_target_mV,
      (unsigned long)data->vout_mV,
      (unsigned long)control->current_target_mA,
      (unsigned long)data->iout_mA,
      (unsigned long)data->vin_mV,
      (long)data->temperature_centi_C[0],
      (long)data->temperature_centi_C[1],
      (long)data->temperature_centi_C[2],
      (long)data->temperature_centi_C[3],
      Bleeder_IsEnabled() ? 1U : 0U,
      (unsigned int)FanRequest_Percent(),
      (unsigned int)(HAL_GPIO_ReadPin(PGOOD_5V_IN_GPIO_Port, PGOOD_5V_IN_Pin)
                     == PGOOD_ASSERTED_LEVEL),
      (unsigned int)(HAL_GPIO_ReadPin(POWER_KILL_GPIO_Port, POWER_KILL_Pin)
                     == POWER_KILL_ASSERTED_LEVEL),
      (unsigned int)HAL_GPIO_ReadPin(OUT_OFF_GPIO_Port, OUT_OFF_Pin),
      (unsigned int)(HAL_GPIO_ReadPin(CC_CV_STATE_GPIO_Port, CC_CV_STATE_Pin)
                     == CC_CV_STATE_CC_LEVEL),
      (s_fault != NULL) ? s_fault : "NONE");
  if ((written > 0) && ((size_t)written < sizeof(line)))
  {
    (void)UART_Protocol_QueueText(line);
  }
}

static bool console_parse_set(const char *line, uint32_t *voltage_mV,
                              uint32_t *current_mA)
{
  const char *cursor = line;

  if (!console_take_token(&cursor, "SET"))
  {
    return false;
  }
  console_skip_spaces(&cursor);
  if (!console_take_token(&cursor, "V=")
      || !console_parse_milli(&cursor, voltage_mV))
  {
    return false;
  }
  if (*cursor == 'V')
  {
    ++cursor;
  }
  console_skip_spaces(&cursor);
  if (!console_take_token(&cursor, "I=")
      || !console_parse_milli(&cursor, current_mA))
  {
    return false;
  }
  if (*cursor == 'A')
  {
    ++cursor;
  }
  console_skip_spaces(&cursor);
  return *cursor == '\0';
}

static void console_normalize_line(char *line)
{
  size_t read_index = 0U;
  size_t write_index = 0U;
  bool pending_space = false;

  while (line[read_index] != '\0')
  {
    unsigned char character = (unsigned char)line[read_index++];

    if (isspace(character) != 0)
    {
      if (write_index > 0U)
      {
        pending_space = true;
      }
      continue;
    }
    if (pending_space)
    {
      line[write_index++] = ' ';
      pending_space = false;
    }
    line[write_index++] = (char)toupper(character);
  }
  line[write_index] = '\0';
}

static void console_handle_line(char *line, uint32_t now)
{
  uint32_t voltage_mV;
  uint32_t current_mA;
  const char *fault;

  (void)now;
  console_normalize_line(line);

  if (console_parse_set(line, &voltage_mV, &current_mA))
  {
    char response[80];

    if (!UART_Console_ApplySetpoint(voltage_mV, current_mA))
    {
      (void)UART_Protocol_QueueText(
          "NACK RANGE V=0.000..27.000V I=0.000..5.000A\r\n");
      return;
    }
    (void)snprintf(response, sizeof(response),
                   "ACK SET V=%lu.%03lu I=%lu.%03lu OUT=%s\r\n",
                   (unsigned long)(voltage_mV / 1000U),
                   (unsigned long)(voltage_mV % 1000U),
                   (unsigned long)(current_mA / 1000U),
                   (unsigned long)(current_mA % 1000U),
                   Control_GetStatus()->output_enabled ? "ON" : "OFF");
    (void)UART_Protocol_QueueText(response);
    return;
  }

  if ((strcmp(line, "OUT ON") == 0)
      || (strcmp(line, "OUTPUT ON") == 0)
      || (strcmp(line, "OUT=ON") == 0)
      || (strcmp(line, "ON") == 0))
  {
    fault = UART_Console_SetOutput(true);
    if (fault != NULL)
    {
      char response[72];
      (void)snprintf(response, sizeof(response),
                     "NACK OUT ON REASON=%s\r\n", fault);
      (void)UART_Protocol_QueueText(response);
      return;
    }
    (void)UART_Protocol_QueueText("ACK OUT ON\r\n");
    return;
  }

  if ((strcmp(line, "OUT OFF") == 0)
      || (strcmp(line, "OUTPUT OFF") == 0)
      || (strcmp(line, "OUT=OFF") == 0)
      || (strcmp(line, "OFF") == 0))
  {
    (void)UART_Console_SetOutput(false);
    (void)UART_Protocol_QueueText("ACK OUT OFF\r\n");
    return;
  }

  (void)UART_Protocol_QueueText("NACK UNKNOWN; USE SET V=.. I=.. | OUT ON | OUT OFF\r\n");
}

const char *UART_Console_GetFault(void)
{
  return (s_fault != NULL) ? s_fault : "NONE";
}

void UART_Console_Init(bool mcp_ok, bool dac_ok)
{
  s_mcp_ok = mcp_ok;
  s_dac_ok = dac_ok;
  s_fault = "NONE";
  s_fault_candidate = NULL;
  s_fault_candidate_since = 0U;
}

void UART_Console_Task(uint32_t now)
{
  char line[CONSOLE_LINE_SIZE];
  const char *fault;

  while (UART_Protocol_ReadLine(line, sizeof(line)))
  {
    console_handle_line(line, now);
  }

  if (!Control_GetStatus()->output_enabled)
  {
    return;
  }
  fault = console_runtime_fault(now);
  if (fault != NULL)
  {
    char response[80];

    Control_SetOutputEnabled(false);
    s_fault = fault;
    /* Push the fault to G4 immediately instead of waiting for the 100 ms slot. */
    UART_Protocol_QueueTelemetry();
    (void)snprintf(response, sizeof(response),
                   "NACK FAULT=%s; OUTPUT FORCED OFF\r\n", fault);
    (void)UART_Protocol_QueueText(response);
  }
}
