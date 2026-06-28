#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "main.h"

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
#define DAC8562_USE_INTERNAL_REFERENCE      1U
#define DAC8562_SPI_TIMEOUT_MS              10U
/* Default orderable address is 01; TODO: verify the fitted part marking. */
#define MCP3464_DEVICE_ADDRESS              0x01U
#define MCP3464_SPI_TIMEOUT_MS              10U
#define MCP3464_INTERNAL_VREF_MV            2400U

#endif /* APP_CONFIG_H */
