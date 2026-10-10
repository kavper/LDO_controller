# VOUT measurement calibration — this physical board

Reference: user readings from Fluke 87V, no external load, local sense.
G0 telemetry was captured concurrently over receive-only UART.

| Set V | DMM V | Original G0 VOUT V |
|---:|---:|---:|
|1|1.005|1.021000|
|3|3.010|3.056004|
|5|5.011|5.088642|
|10|10.009|10.166518|
|20|20.01|20.323553|
|25|25.01|25.403640|

A least-squares gain fit through zero gives multiplier 0.98453636995.
The existing conversion divides raw code by gain_ppm, so the stored divisor
is 1015707 ppm, not 984536. Zero_raw remains zero: clipped zero telemetry
cannot identify a signed ADC offset. These coefficients are board specific.

Output OFF: user measured 0 mV. Output ON with setpoint zero: user measured
3.1 mV and G0 reported 3 mV. Do not subtract that physical LDO residual from
ADC measurements to make the display read zero. A DAC code cannot go below
zero; true-zero output regulation remains a separate hardware/control issue.

Only VOUT ADC scaling is adjusted. VIN, current, DAC readbacks and DAC output
setpoints are not calibrated by this change. The initial voltage setpoint
errors of 5–11 mV need separate DAC/LDO validation; the normal 60 V range
readings have 10 mV display resolution, so no sub-millivolt accuracy is claimed.
The corrected VOUT feeds both telemetry and existing voltage protection.

Validation: production conversion host regression with reconstructed ADC codes
from millivolt telemetry (quantization included), Release compilation. Recheck
an independent point such as 7 V after flashing; no post-flash verification yet.
