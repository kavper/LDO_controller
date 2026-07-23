#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "main.h"

/*
 * Incremental board bring-up:
 *   1 - safe solid LED + UART heartbeat only
 *   2 - blinking LED + UART + internal ADC1/PGOOD diagnostics
 *   3 - MCP3464 verification + low-level DAC8562/readback test
 *   0 - normal controller application
 *
 * Keep the active stage enabled until its hardware has been verified.
 */
#define APP_BRINGUP_STAGE                   3U
#define BOARD_LED_GPIO_PORT                 GPIOB
#define BOARD_LED_PIN                       GPIO_PIN_0

/* User-facing setpoint limits. */
#define APP_VOLTAGE_MIN_MV                 0U
#define APP_VOLTAGE_MAX_MV                 30000U
#define APP_CURRENT_MIN_MA                 0U
#define APP_CURRENT_MAX_MA                 5000U

/* Cooperative scheduler periods. */
#define APP_CONTROL_PERIOD_MS              1U
#define APP_TELEMETRY_PERIOD_MS            20U

/* Conservative initial ramp values; tune after analog-loop validation. */
#define CONTROL_VOLTAGE_RAMP_MV_PER_MS     50U
#define CONTROL_CURRENT_RAMP_MA_PER_MS     10U
#define CONTROL_MODE_FILTER_MS             10U

/* Preregulator request limits. TODO: confirm against the preregulator hardware. */
#define VPRE_MIN_MV                        3000U
#define VPRE_MAX_MV                        36000U
#define VPRE_MARGIN_MV                     3000U

/* Bleeder hysteresis. */
#define BLEEDER_ON_THRESHOLD_MV            500U
#define BLEEDER_OFF_THRESHOLD_MV           200U
#define BLEEDER_OFF_CONFIRM_MS             500U

/* GPIO polarities are centralized here so board revisions need one change. */
#define OUT_OFF_ASSERTED_LEVEL             GPIO_PIN_SET
#define OUT_OFF_DEASSERTED_LEVEL           GPIO_PIN_RESET
#define BLEEDER_ENABLED_LEVEL              GPIO_PIN_SET
#define BLEEDER_DISABLED_LEVEL             GPIO_PIN_RESET

/* TODO: confirm the CC/CV comparator polarity on the final schematic. */
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
 * Stage-3 board calibration, calculated from 70 DAC-to-ADC loopback samples.
 * Gain values are expressed in ppm relative to the ideal transfer function.
 * The common gain is the mean of the CC and CV loopback slopes and is used
 * provisionally for VOUT, IOUT and VIN until an external meter calibration.
 */
#define MCP3464_COMMON_GAIN_PPM              995896L
#define MCP3464_DAC_CC_GAIN_PPM              998688L
#define MCP3464_DAC_CV_GAIN_PPM              993103L
#define MCP3464_VOUT_ZERO_RAW                (-12L)
#define MCP3464_IOUT_ZERO_RAW                14L
#define MCP3464_VIN_ZERO_RAW                 (-7L)
#define MCP3464_DAC_CC_ZERO_RAW              (-3L)
#define MCP3464_DAC_CV_ZERO_RAW              (-3L)

/*
 * Analog measurement paths from the board schematic/BOM.
 * CURRENT_SENSE_AMPLIFIER_GAIN = 11 assumes R84 fitted and R87 not fitted.
 * If R87 is used instead, the gain is 10. Never fit both: that bypasses R69.
 */
#define VOLTAGE_SENSE_INPUT_RESISTANCE_OHM   160000L
#define VOLTAGE_SENSE_FEEDBACK_RESISTANCE_OHM 17400L
#define CURRENT_SENSE_SHUNT_MILLIOHM         50L
#define CURRENT_SENSE_AMPLIFIER_GAIN         11L

#endif /* APP_CONFIG_H */
