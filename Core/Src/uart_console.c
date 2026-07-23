#include "uart_console.h"

#include "app_config.h"
#include "bleeder.h"
#include "control.h"
#include "main.h"
#include "measurements.h"
#include "uart_protocol.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CONSOLE_LINE_SIZE 96U
#define CONSOLE_TABLE_CONTENT_WIDTH 76U

static bool s_mcp_ok;
static bool s_dac_ok;
static const char *s_fault;
static uint32_t s_dac_settle_until;
static const char *s_fault_candidate;
static uint32_t s_fault_candidate_since;

static uint32_t console_abs_difference(uint32_t first, uint32_t second)
{
  return (first >= second) ? (first - second) : (second - first);
}

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

/* Parses a non-negative value with up to three decimals into milli-units. */
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

static const char *console_mode_name(Control_Mode_t mode)
{
  switch (mode)
  {
    case CONTROL_MODE_CV:
      return "CV";
    case CONTROL_MODE_CC:
      return "CC";
    default:
      return "OFF";
  }
}

static uint32_t console_temperature_magnitude(int32_t centi_C)
{
  return (uint32_t)((centi_C < 0) ? -(int64_t)centi_C : (int64_t)centi_C);
}

static char console_temperature_sign(int32_t centi_C)
{
  return (centi_C < 0) ? '-' : '+';
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
  uint32_t expected_cv_mV;
  uint32_t expected_cc_mV;

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
  if (data->vin_mV < CONSOLE_MINIMUM_VIN_MV)
  {
    *confirm_ms = CONSOLE_VIN_CONFIRM_MS;
    return "VIN_LOW";
  }

  /*
   * On a downward setpoint change the DAC follows voltage_applied_mV at the
   * configured slew rate. Comparing VOUT immediately with the lower final
   * target would misclassify the intentional 10 V -> 1 V transition as an
   * overvoltage. Use the higher of target and applied so protection follows
   * the commanded trajectory in both ramp directions.
   */
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
  if (!console_temperatures_safe(data))
  {
    *confirm_ms = CONSOLE_TEMPERATURE_CONFIRM_MS;
    return "TEMP_HIGH";
  }
  if ((int32_t)(now - s_dac_settle_until) < 0)
  {
    return NULL;
  }

  expected_cv_mV = Control_DacRawToMillivolts(
      Control_VoltageToDacRaw(control->voltage_applied_mV));
  expected_cc_mV = Control_DacRawToMillivolts(
      Control_CurrentToDacRaw(control->current_applied_mA));
  if (console_abs_difference(data->dac_cv_readback_mV, expected_cv_mV)
      > CONSOLE_DAC_READBACK_TOLERANCE_MV)
  {
    *confirm_ms = CONSOLE_DAC_READBACK_CONFIRM_MS;
    return "DAC_CV_FB";
  }
  if (console_abs_difference(data->dac_cc_readback_mV, expected_cc_mV)
      > CONSOLE_DAC_READBACK_TOLERANCE_MV)
  {
    *confirm_ms = CONSOLE_DAC_READBACK_CONFIRM_MS;
    return "DAC_CC_FB";
  }
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

static void console_queue_help(void)
{
  (void)UART_Protocol_QueueText(
      "\r\nCommands (115200 8N1, end with ENTER):\r\n"
      "  SET V=5.000 I=0.100  - set 5.000 V and 0.100 A limit\r\n"
      "  OUT ON               - guarded output enable\r\n"
      "  OUT OFF              - immediate output disable\r\n"
      "  STATUS               - print table now\r\n"
      "  HELP                 - print this help\r\n");
}

static bool console_append_rule(char *buffer, size_t capacity, size_t *used,
                                char fill)
{
  size_t index;

  if ((capacity - *used) < (CONSOLE_TABLE_CONTENT_WIDTH + 7U))
  {
    return false;
  }
  buffer[(*used)++] = '+';
  for (index = 0U; index < (CONSOLE_TABLE_CONTENT_WIDTH + 2U); ++index)
  {
    buffer[(*used)++] = fill;
  }
  buffer[(*used)++] = '+';
  buffer[(*used)++] = '\r';
  buffer[(*used)++] = '\n';
  buffer[*used] = '\0';
  return true;
}

static bool console_append_row(char *buffer, size_t capacity, size_t *used,
                               const char *format, ...)
{
  char content[160];
  va_list arguments;
  int content_length;
  int written;

  va_start(arguments, format);
  content_length = vsnprintf(content, sizeof(content), format, arguments);
  va_end(arguments);
  if ((content_length < 0)
      || (content_length > (int)CONSOLE_TABLE_CONTENT_WIDTH))
  {
    return false;
  }

  written = snprintf(buffer + *used, capacity - *used,
                     "| %-*s |\r\n",
                     (int)CONSOLE_TABLE_CONTENT_WIDTH, content);
  if ((written < 0) || ((size_t)written >= (capacity - *used)))
  {
    return false;
  }
  *used += (size_t)written;
  return true;
}

void UART_Console_QueueStatus(void)
{
  const Measurements_Data_t *data = Measurements_GetData();
  const Control_Status_t *control = Control_GetStatus();
  uint32_t temperature[MEASUREMENTS_TEMPERATURE_COUNT];
  uint16_t cv_code = Control_VoltageToDacRaw(control->voltage_applied_mV);
  uint16_t cc_code = Control_CurrentToDacRaw(control->current_applied_mA);
  static char report[1450];
  size_t used = 0U;
  uint8_t index;
  bool ok = true;

  for (index = 0U; index < MEASUREMENTS_TEMPERATURE_COUNT; ++index)
  {
    temperature[index] =
        console_temperature_magnitude(data->temperature_centi_C[index]);
  }

  report[0] = '\0';
  ok = console_append_rule(report, sizeof(report), &used, '=');
  ok = ok && console_append_row(report, sizeof(report), &used,
      "LDO CONTROLLER LIVE DEBUG");
  ok = ok && console_append_row(report, sizeof(report), &used,
      "STATE: OUT=%s  MODE=%s  BLEED=%s  PGOOD=%u  FAULT=%s",
      control->output_enabled ? "ON" : "OFF",
      console_mode_name(control->mode),
      Bleeder_IsEnabled() ? "ON" : "OFF",
      (unsigned int)(HAL_GPIO_ReadPin(PGOOD_5V_IN_GPIO_Port,
                                     PGOOD_5V_IN_Pin)
                     == PGOOD_ASSERTED_LEVEL),
      s_fault);
  ok = ok && console_append_rule(report, sizeof(report), &used, '-');
  ok = ok && console_append_row(report, sizeof(report), &used,
      "CONTROL (requested / applied / DAC code / measured)");
  ok = ok && console_append_row(report, sizeof(report), &used,
      "Voltage: %lu.%03lu V / %lu.%03lu V / %05u / %lu.%03lu V",
      (unsigned long)(control->voltage_target_mV / 1000U),
      (unsigned long)(control->voltage_target_mV % 1000U),
      (unsigned long)(control->voltage_applied_mV / 1000U),
      (unsigned long)(control->voltage_applied_mV % 1000U),
      (unsigned int)cv_code,
      (unsigned long)(data->vout_mV / 1000U),
      (unsigned long)(data->vout_mV % 1000U));
  ok = ok && console_append_row(report, sizeof(report), &used,
      "Current: %lu.%03lu A / %lu.%03lu A / %05u / N/A (U18 missing)",
      (unsigned long)(control->current_target_mA / 1000U),
      (unsigned long)(control->current_target_mA % 1000U),
      (unsigned long)(control->current_applied_mA / 1000U),
      (unsigned long)(control->current_applied_mA % 1000U),
      (unsigned int)cc_code);
  ok = ok && console_append_rule(report, sizeof(report), &used, '-');
  ok = ok && console_append_row(report, sizeof(report), &used,
      "MCP3464: VIN=%lu.%03lu V  DAC_CV=%lu.%03lu V  DAC_CC=%lu.%03lu V",
      (unsigned long)(data->vin_mV / 1000U),
      (unsigned long)(data->vin_mV % 1000U),
      (unsigned long)(data->dac_cv_readback_mV / 1000U),
      (unsigned long)(data->dac_cv_readback_mV % 1000U),
      (unsigned long)(data->dac_cc_readback_mV / 1000U),
      (unsigned long)(data->dac_cc_readback_mV % 1000U));
  ok = ok && console_append_row(report, sizeof(report), &used,
      "TEMP T1 MOS=%c%lu.%02lu C  T2 AMBIENT=%c%lu.%02lu C",
      console_temperature_sign(data->temperature_centi_C[0]),
      (unsigned long)(temperature[0] / 100U),
      (unsigned long)(temperature[0] % 100U),
      console_temperature_sign(data->temperature_centi_C[1]),
      (unsigned long)(temperature[1] / 100U),
      (unsigned long)(temperature[1] % 100U));
  ok = ok && console_append_row(report, sizeof(report), &used,
      "TEMP T3 BLEED=%c%lu.%02lu C  T4 POWER=%c%lu.%02lu C",
      console_temperature_sign(data->temperature_centi_C[2]),
      (unsigned long)(temperature[2] / 100U),
      (unsigned long)(temperature[2] % 100U),
      console_temperature_sign(data->temperature_centi_C[3]),
      (unsigned long)(temperature[3] / 100U),
      (unsigned long)(temperature[3] % 100U));
  ok = ok && console_append_rule(report, sizeof(report), &used, '-');
  ok = ok && console_append_row(report, sizeof(report), &used,
      "RAW MCP: VIN=%ld  VOUT=%ld  IOUT=%ld",
      (long)data->vin_diff_raw, (long)data->vout_diff_raw,
      (long)data->iout_diff_raw);
  ok = ok && console_append_row(report, sizeof(report), &used,
      "RAW DAC FEEDBACK: CC=%ld  CV=%ld",
      (long)data->dac_cc_readback_raw, (long)data->dac_cv_readback_raw);
  ok = ok && console_append_row(report, sizeof(report), &used,
      "RAW STM: T1=%u  T2=%u  T3=%u  T4=%u",
      (unsigned int)data->temperature_filtered[0],
      (unsigned int)data->temperature_filtered[1],
      (unsigned int)data->temperature_filtered[2],
      (unsigned int)data->temperature_filtered[3]);
  ok = ok && console_append_rule(report, sizeof(report), &used, '=');

  if (ok)
  {
    (void)UART_Protocol_QueueText(report);
  }
}

static void console_ack_set(uint32_t voltage_mV, uint32_t current_mA)
{
  const bool output_enabled = Control_GetStatus()->output_enabled;
  char response[144];

  (void)snprintf(
      response, sizeof(response),
      "ACK SET V=%lu.%03luV I=%lu.%03luA CV_CODE=%u CC_CODE=%u "
      "OUTPUT=%s%s\r\n",
      (unsigned long)(voltage_mV / 1000U),
      (unsigned long)(voltage_mV % 1000U),
      (unsigned long)(current_mA / 1000U),
      (unsigned long)(current_mA % 1000U),
      (unsigned int)Control_VoltageToDacRaw(voltage_mV),
      (unsigned int)Control_CurrentToDacRaw(current_mA),
      output_enabled ? "ON" : "OFF",
      output_enabled ? "" : " (SEND OUT ON)");
  (void)UART_Protocol_QueueText(response);
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
  console_normalize_line(line);

  if (console_parse_set(line, &voltage_mV, &current_mA))
  {
    if ((voltage_mV > APP_VOLTAGE_MAX_MV)
        || (current_mA > APP_CURRENT_MAX_MA))
    {
      (void)UART_Protocol_QueueText(
          "NACK RANGE V=0.000..27.000V I=0.000..5.000A\r\n");
      return;
    }
    Control_SetVoltageTarget(voltage_mV);
    Control_SetCurrentTarget(current_mA);
    s_dac_settle_until = now + CONSOLE_DAC_SETTLE_MS;
    s_fault_candidate = NULL;
    s_fault = "NONE";
    console_ack_set(voltage_mV, current_mA);
    return;
  }

  if ((strcmp(line, "OUT ON") == 0)
      || (strcmp(line, "OUTPUT ON") == 0)
      || (strcmp(line, "OUT=ON") == 0)
      || (strcmp(line, "ON") == 0))
  {
    if (Control_GetStatus()->output_enabled)
    {
      (void)UART_Protocol_QueueText("ACK OUT ON (ALREADY ON)\r\n");
      return;
    }
    fault = console_preflight_fault();
    if (fault != NULL)
    {
      char response[72];
      (void)snprintf(response, sizeof(response),
                     "NACK OUT ON REASON=%s\r\n", fault);
      (void)UART_Protocol_QueueText(response);
      return;
    }
    s_fault = "NONE";
    s_dac_settle_until = now + CONSOLE_DAC_SETTLE_MS;
    s_fault_candidate = NULL;
    Control_SetOutputEnabled(true);
    (void)UART_Protocol_QueueText("ACK OUT ON\r\n");
    return;
  }

  if ((strcmp(line, "OUT OFF") == 0)
      || (strcmp(line, "OUTPUT OFF") == 0)
      || (strcmp(line, "OUT=OFF") == 0)
      || (strcmp(line, "OFF") == 0))
  {
    Control_SetOutputEnabled(false);
    s_fault = "NONE";
    s_fault_candidate = NULL;
    (void)UART_Protocol_QueueText("ACK OUT OFF\r\n");
    return;
  }

  if (strcmp(line, "STATUS") == 0)
  {
    (void)UART_Protocol_QueueText("ACK STATUS\r\n");
    UART_Console_QueueStatus();
    return;
  }

  if (strcmp(line, "HELP") == 0)
  {
    (void)UART_Protocol_QueueText("ACK HELP\r\n");
    console_queue_help();
    return;
  }

  {
    char response[160];
    (void)snprintf(
        response, sizeof(response),
        "NACK UNKNOWN CMD=\"%.80s\"; USE SET ... | OUT ON | OUT OFF | STATUS | HELP\r\n",
        line);
    (void)UART_Protocol_QueueText(response);
  }
}

void UART_Console_Init(bool mcp_ok, bool dac_ok)
{
  s_mcp_ok = mcp_ok;
  s_dac_ok = dac_ok;
  s_fault = "NONE";
  s_dac_settle_until = 0U;
  s_fault_candidate = NULL;
  s_fault_candidate_since = 0U;
  console_queue_help();
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
    (void)snprintf(response, sizeof(response),
                   "NACK FAULT=%s; OUTPUT FORCED OFF\r\n", fault);
    (void)UART_Protocol_QueueText(response);
  }
}
