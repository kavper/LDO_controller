#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "main.h"

#define BOARD_LED_GPIO_PORT                 LED_G0_GPIO_Port
#define BOARD_LED_PIN                       LED_G0_Pin

/* User-facing setpoint limits. */
#define APP_VOLTAGE_MIN_MV                 0U
#define APP_VOLTAGE_MAX_MV                 27000U
#define APP_CURRENT_MIN_MA                 0U
#define APP_CURRENT_MAX_MA                 5000U

/* Cooperative scheduler periods. */
#define APP_CONTROL_PERIOD_MS              1U
#define APP_TELEMETRY_PERIOD_MS            200U

/* Conservative initial ramp values; tune after analog-loop validation. */
#define CONTROL_VOLTAGE_RAMP_MV_PER_MS     50U
#define CONTROL_CURRENT_RAMP_MA_PER_MS     10U
#define CONTROL_MODE_FILTER_MS             10U

/* Preregulator request limits. TODO: confirm against the preregulator hardware. */
#define VPRE_MIN_MV                        3000U
#define VPRE_MAX_MV                        36000U
#define VPRE_MARGIN_MV                     3000U

/*
 * Bleeder request (G4 drives BLEED_ON; G0 has no GPIO on this revision).
 * With the output enabled, bleed below a 4.000 V setpoint so the analog
 * loops see a minimum load. Hysteresis avoids chatter around 4 V.
 * With the output disabled the bleeder still discharges VOUT to ~0.2 V.
 */
#define BLEEDER_RUN_ON_BELOW_MV            4000U
#define BLEEDER_RUN_OFF_ABOVE_MV           4200U
#define BLEEDER_ON_THRESHOLD_MV            500U
#define BLEEDER_OFF_THRESHOLD_MV           200U
#define BLEEDER_OFF_CONFIRM_MS             500U

/*
 * Fan duty request sent to G4 (G4 owns FAN_PWM / FAN_TACH). Linear between
 * OFF and FULL using the hottest of MOSFET / bleeder / PSU-area NTCs.
 */
#define FAN_REQUEST_OFF_CENTI_C            3000
#define FAN_REQUEST_FULL_CENTI_C           5500
#define FAN_REQUEST_MIN_PERCENT            20U
#define FAN_REQUEST_FAILSAFE_PERCENT       40U

/*
 * Analog OUT-OFF (G0 2026-08-30): D15/D16 diode-OR into U18.
 * POWER_KILL or STM_OUT_OFF HIGH → analog FETs off.
 * Idle pull-up on POWER_KILL is therefore kill. G4 POWER_PERMIT must
 * turn the opto on and pull POWER_KILL LOW before OUT ON can produce VOUT.
 */
#define OUT_OFF_ASSERTED_LEVEL             GPIO_PIN_SET
#define OUT_OFF_DEASSERTED_LEVEL           GPIO_PIN_RESET
#define POWER_KILL_ASSERTED_LEVEL          GPIO_PIN_SET
/* BLEED_ON is not connected to the MCU on this revision. */

/* STM_CC_CV is open-collector Q16 + 10 k pull-up; DS2 lights when the net is high. */
#define CC_CV_STATE_CC_LEVEL               GPIO_PIN_SET
#define PGOOD_ASSERTED_LEVEL               GPIO_PIN_SET

/* Device/interface configuration. */
#define DAC8562_USE_INTERNAL_REFERENCE      0U
#define DAC8562_SPI_TIMEOUT_MS              10U
/* MCP3464T-E/NC has default SPI address 01 and uses the external 3V_REF. */
#define MCP3464_DEVICE_ADDRESS              0x01U
#define MCP3464_SPI_TIMEOUT_MS              10U
#define MCP3464_EXTERNAL_VREF_MV            3000U

/*
 * NTC: 10 k pull-up to 3V_REFR, STM32 ADC1 VREF+ is +3V3R.
 * RT1/RT2 103AT-2; T3/T4 10 k NTC.
 */
#define TEMPERATURE_ADC_REFERENCE_MV        3300U
#define TEMPERATURE_DIVIDER_SUPPLY_MV       3000U
#define TEMPERATURE_NTC_NOMINAL_OHM         10000U
#define TEMPERATURE_NTC_NOMINAL_KELVIN_X100 29815U
#define TEMPERATURE_NTC_BETA_103AT2_K       3435U
#define TEMPERATURE_NTC_BETA_NCP18_K        3434U

/* Nominal MCP3464 scale (DMM calibration later). */
#define MCP3464_VIN_GAIN_PPM                 1000000L
#define MCP3464_VOUT_GAIN_PPM                1000000L
#define MCP3464_IOUT_GAIN_PPM                1000000L
#define MCP3464_DAC_CC_GAIN_PPM              1000000L
#define MCP3464_DAC_CV_GAIN_PPM              1000000L
#define MCP3464_VOUT_ZERO_RAW                0L
#define MCP3464_IOUT_ZERO_RAW                0L
#define MCP3464_VIN_ZERO_RAW                 0L
#define MCP3464_DAC_CC_ZERO_RAW              0L
#define MCP3464_DAC_CV_ZERO_RAW              0L

/*
 * Analog front-end, G0 sheet 2026-08-30, no DMM trim.
 * VOUT U34: DC gain 15k/180k. VIN U30: 15k/(180k+33k).
 * IOUT INA241A1 gain 10, shunt R108 50 mOhm, ADC sees 3V_REFR - Vout.
 * Analog CC I_MON U32A gain 10k/1k = 10.
 */
#define VOUT_DIFFAMP_INPUT_OHM               180000L
#define VOUT_DIFFAMP_FEEDBACK_OHM            15000L
#define VIN_DIFFAMP_INPUT_OHM                213000L
#define VIN_DIFFAMP_FEEDBACK_OHM             15000L
#define CURRENT_SENSE_SHUNT_MILLIOHM         50L
#define CURRENT_SENSE_AMPLIFIER_GAIN         10L
#define CURRENT_LIMIT_AMPLIFIER_GAIN         10L

/* Interactive console safety thresholds. */
#define CONSOLE_TLM_PERIOD_MS                200U
#define CONSOLE_MINIMUM_VIN_MV              4500U
#define CONSOLE_MAXIMUM_TEMPERATURE_CENTI_C 6000L
#define CONSOLE_VOUT_OVERSHOOT_MIN_MV       1500U
#define CONSOLE_VOUT_OVERSHOOT_PERCENT         10U
#define CONSOLE_VOUT_HARD_OVERSHOOT_MIN_MV  3000U
#define CONSOLE_VOUT_HARD_OVERSHOOT_PERCENT    20U
#define CONSOLE_DAC_READBACK_TOLERANCE_MV     75U
#define CONSOLE_DAC_SETTLE_MS                 750U
#define CONSOLE_PGOOD_CONFIRM_MS               50U
#define CONSOLE_VIN_CONFIRM_MS                250U
#define CONSOLE_VOUT_OV_CONFIRM_MS            100U
#define CONSOLE_VOUT_HARD_OV_CONFIRM_MS        10U
#define CONSOLE_TEMPERATURE_CONFIRM_MS        500U
#define CONSOLE_DAC_READBACK_CONFIRM_MS       300U

#endif /* APP_CONFIG_H */
