# Current ADC and CC DAC calibration — this physical board

Reference: Fluke 87V in series with 6.5 ohm / 200 W resistor. Voltage setpoint
27 V, current varied, mode confirmed CC in received G0 telemetry.

| Set A | DMM A | Original G0 ADC A |
|---:|---:|---:|
|0.100|0.101|0.101732|
|0.250|0.253|0.255228|
|0.500|0.505|0.511088|
|1.000|1.007|1.022000|
|2.000|2.014|2.044002|
|3.000|3.021|3.067000|
|4.000|4.029|4.088917|

The 4 A reading was collected after fitting and is held out from the fit.

ADC: gain-only least squares for 0.1–3 A. Store divisor 1015022 ppm, since
measurements_apply_calibration divides by gain. Zero_raw is unchanged: the
existing absolute-value current measurement and rounded telemetry cannot
identify signed raw offset. Predicted corrected measurement at held-out 4 A
is 4.028402 A versus DMM 4.029 A. Current protection uses the corrected value.

CC DAC/LDO: actual measured current versus original rounded DAC code expressed
as nominal current. Affine fit gain 1006617 ppm, offset 947 microamps. Invert
that model in Control_CurrentToDacRaw; preserve physical target/ramp/current
limit values and zero-code at zero request. Saturate rather than wrap codes.

The correction applies to the whole CC control path, not independently to DAC
silicon. Existing CV DAC and VOUT ADC calibration remains in place. VIN and
DAC readback channels remain uncalibrated. ADC current uses absolute magnitude;
this change does not add bidirectional current sensing or calibrated raw offset.

Validation: compiled actual production ADC/CC DAC functions; all six fitting
points plus held-out 4 A; ADC signed-magnitude, zero, monotonic 0–5 A, saturation;
previous CV/VOUT regressions; G0 Release build. Millivolt/milliamp telemetry
quantization and Fluke uncertainty limit interpretation of fit residuals.
These are predictions, not post-flash measured accuracy. After flashing check
an independent CC target such as 0.750 A and compare both DMM and H7.
