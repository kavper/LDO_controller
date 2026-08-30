#include "board_led.h"

#include "app_config.h"
#include "control.h"
#include "main.h"
#include "uart_console.h"

#include <string.h>

/* DS3 is wired LED + R157 to +3V3R, cathode to PB0 → active low. */
#define BOARD_LED_ON_LEVEL   GPIO_PIN_RESET
#define BOARD_LED_OFF_LEVEL  GPIO_PIN_SET

typedef enum
{
  BOARD_LED_PATTERN_HEARTBEAT = 0U,
  BOARD_LED_PATTERN_OUTPUT_CV,
  BOARD_LED_PATTERN_OUTPUT_CC,
  BOARD_LED_PATTERN_KILL,
  BOARD_LED_PATTERN_FAULT
} BoardLedPattern_t;

static BoardLedPattern_t s_pattern;
static uint32_t s_phase_tick;
static uint8_t s_phase;

static void board_led_write(bool on)
{
  HAL_GPIO_WritePin(BOARD_LED_GPIO_PORT, BOARD_LED_PIN,
                    on ? BOARD_LED_ON_LEVEL : BOARD_LED_OFF_LEVEL);
}

static bool board_led_power_kill_asserted(void)
{
  return HAL_GPIO_ReadPin(POWER_KILL_GPIO_Port, POWER_KILL_Pin) == GPIO_PIN_RESET;
}

static bool board_led_pgood_ok(void)
{
  return HAL_GPIO_ReadPin(PGOOD_5V_IN_GPIO_Port, PGOOD_5V_IN_Pin)
      == PGOOD_ASSERTED_LEVEL;
}

static BoardLedPattern_t board_led_select_pattern(void)
{
#if APP_BRINGUP_STAGE == 6U
  const char *fault = UART_Console_GetFault();
  if ((fault != NULL) && (strcmp(fault, "NONE") != 0))
  {
    return BOARD_LED_PATTERN_FAULT;
  }
#endif

  if (board_led_power_kill_asserted() || !board_led_pgood_ok())
  {
    return BOARD_LED_PATTERN_KILL;
  }

#if (APP_BRINGUP_STAGE == 0U) || (APP_BRINGUP_STAGE == 6U)
  {
    const Control_Status_t *status = Control_GetStatus();
    if (status->output_enabled)
    {
      return (status->mode == CONTROL_MODE_CC)
               ? BOARD_LED_PATTERN_OUTPUT_CC
               : BOARD_LED_PATTERN_OUTPUT_CV;
    }
  }
#endif

  return BOARD_LED_PATTERN_HEARTBEAT;
}

void BoardLed_Init(void)
{
  s_pattern = BOARD_LED_PATTERN_HEARTBEAT;
  s_phase_tick = 0U;
  s_phase = 0U;
  board_led_write(false);
}

void BoardLed_Task(uint32_t now)
{
  BoardLedPattern_t next = board_led_select_pattern();
  uint32_t elapsed;
  uint32_t step_ms;
  bool led_on;

  if (next != s_pattern)
  {
    s_pattern = next;
    s_phase = 0U;
    s_phase_tick = now;
  }

  elapsed = (uint32_t)(now - s_phase_tick);

  switch (s_pattern)
  {
    case BOARD_LED_PATTERN_OUTPUT_CV:
      board_led_write(true);
      return;

    case BOARD_LED_PATTERN_OUTPUT_CC:
      step_ms = 120U;
      if (elapsed >= step_ms)
      {
        s_phase_tick = now;
        s_phase ^= 1U;
      }
      board_led_write(s_phase == 0U);
      return;

    case BOARD_LED_PATTERN_FAULT:
      step_ms = 80U;
      if (elapsed >= step_ms)
      {
        s_phase_tick = now;
        s_phase ^= 1U;
      }
      board_led_write(s_phase == 0U);
      return;

    case BOARD_LED_PATTERN_KILL:
      /* Two short flashes, then a pause: 5V rail or G4 POWER_PERMIT kill. */
      if (s_phase == 0U)
      {
        led_on = true;
        step_ms = 80U;
      }
      else if (s_phase == 1U)
      {
        led_on = false;
        step_ms = 80U;
      }
      else if (s_phase == 2U)
      {
        led_on = true;
        step_ms = 80U;
      }
      else
      {
        led_on = false;
        step_ms = 640U;
      }
      board_led_write(led_on);
      if (elapsed >= step_ms)
      {
        s_phase_tick = now;
        s_phase = (uint8_t)((s_phase + 1U) % 4U);
      }
      return;

    case BOARD_LED_PATTERN_HEARTBEAT:
    default:
      /* Alive: 60 ms flash once per second while output is off and rails are OK. */
      if (s_phase == 0U)
      {
        board_led_write(true);
        step_ms = 60U;
      }
      else
      {
        board_led_write(false);
        step_ms = 940U;
      }
      if (elapsed >= step_ms)
      {
        s_phase_tick = now;
        s_phase ^= 1U;
      }
      return;
  }
}
