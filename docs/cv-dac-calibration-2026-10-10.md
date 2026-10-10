> Current/CC was calibrated subsequently: [cc-calibration-2026-10-10.md](cc-calibration-2026-10-10.md).

# CV DAC/LDO calibration — this physical board

Uses the same unloaded Fluke 87V sweep as VOUT ADC calibration.
The ADC calibration alone changes measurement; this change also adjusts the
CV DAC code used by Control_VoltageToDacRaw. Current DAC/CC is unchanged.

Fit actual output versus the nominal voltage represented by the original
rounded DAC codes at setpoints 1, 3, 5, 10, 20 and 25 V:

    actual_mV = 1.0000937244953991 * nominal_DAC_mV + 8.31954323615355

Store gain 1000094 ppm and offset 8320 microvolts; apply the inverse model
before conversion to the nearest DAC code. This calibrates the whole CV
DAC/LDO path, not the DAC silicon independently. Fit residuals on original
readings are approximately -3.18, +1.54, +2.26, -0.17, -0.01, -0.43 mV.
Those are fit residuals, not measured post-correction errors. The 20/25 V
readings had 10 mV display resolution and DMM uncertainty must be respected.

Voltage ramp and preregulator request remain in physical target millivolts.
ADC correction remains independent. Zero/OFF uses code zero and requests
below the fitted offset clamp to zero. The measured physical 3.1 mV at
ON/zero cannot be eliminated by subtracting more DAC code below zero.
The affine model was fitted over 1–25 V; behaviour below 1 V and under load
requires separate verification. Coefficients apply only to this board.

Validation: actual production function compiled in a host test, monotonic
0–27 V sweep, zero/OFF, overflow/saturation, inverse-fit and individual-sweep
predictions. Existing VOUT ADC calibration test and Release build pass.
No post-flash verification yet. Check 5 V and an independent 7 V point next.
