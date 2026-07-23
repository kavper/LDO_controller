# MCP3464 and DAC8562 stage-3 calibration

## Data set

The statistics below use 70 UART samples from the safe stage-3 loopback test.
The power output and bleeder were disabled. Both DAC channels used gain x1 and
the external `3V_REF`.

The MCP3464 16-bit output format is signed two's complement: one sign bit and
15 magnitude bits. Therefore, for the same reference voltage:

`expected ADC code = DAC code / 2`

| DAC code | Ideal voltage | Samples | CC mean | CC std. dev. | CC raw error | CV mean | CV std. dev. | CV raw error |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0.000 mV | 18 | -3.000 | 0.000 LSB | -3 LSB | -3.000 | 0.000 LSB | -3 LSB |
| 4096 | 187.500 mV | 20 | 2127.450 | 0.510 LSB | +3.879% | 2113.450 | 0.510 LSB | +3.196% |
| 8192 | 375.000 mV | 17 | 4260.000 | 0.000 LSB | +4.004% | 4234.118 | 0.332 LSB | +3.372% |
| 16384 | 750.000 mV | 15 | 8525.467 | 0.516 LSB | +4.071% | 8477.933 | 0.258 LSB | +3.490% |

Raw error includes the approximately -3 LSB zero offset. The error after
removing zero offset is about +4.03% to +4.11% for CC and +3.34% to +3.53%
for CV.

Linear least-squares fits:

- CC: `ADC = -3.9263 + 1.04111775 * expected`, R-squared = 0.99999994
- CV: `ADC = -5.1965 + 1.03536079 * expected`, R-squared = 0.99999965

The sub-LSB standard deviation and near-unity R-squared values show excellent
repeatability and linearity. The main uncalibrated error is gain, not noise.
The MCP3464 data sheet allows up to +/-3% ADC gain error; DAC gain, reference,
and board-path errors add to the complete loopback result.

## Firmware calibration

The firmware uses:

- MCP3464 digital GAINCAL: 31425/32768 = 0.9590149
- final CC loopback gain after GAINCAL: 0.998688
- final CV loopback gain after GAINCAL: 0.993103
- provisional common ADC gain used before external calibration: 0.995896
- measured post-GAINCAL zero offsets: VOUT -12, IOUT +14, VIN -7 ADC codes

After calibration, live UART results were:

| DAC code | Reported DAC_CC | Reported DAC_CV |
|---:|---:|---:|
| 0 | 0 mV | 0 mV |
| 4096 | 187-188 mV | 187 mV |
| 8192 | 375 mV | 375 mV |
| 16384 | 750 mV | 750 mV |

This is a board-level loopback calibration, not an absolute metrology
calibration. One or more measurements with a calibrated external DMM are
required to separate ADC gain, DAC gain and the actual `3V_REF` value.

## External DMM calibration

VIN and VOUT were calibrated independently against an external DMM on
2026-07-23. The MCP3464 data below are post-GAINCAL signed 16-bit codes.

### VIN measurement

| DMM VIN | Mean raw code | Old firmware reading | Residual after calibration |
|---:|---:|---:|---:|
| 8.000 V | 9508.9 | 8.044 V | +0.509 mV |
| 11.000 V | 13076.0 | 11.060 V | -0.446 mV |
| 14.000 V | 16645.5 | 14.078 V | +0.617 mV |
| 17.000 V | 20212.5 | 17.092 V | -0.422 mV |
| 20.000 V | 23781.2 | 20.109 V | -0.031 mV |

The unconstrained least-squares fit is:

`VIN = 0.000840781246 * raw + 0.005347508 V`

Firmware retains the integer zero correction of -7 codes and uses the
least-squares gain constrained to that zero:

- `MCP3464_VIN_ZERO_RAW = -7`
- `MCP3464_VIN_GAIN_PPM = 1001323`
- maximum calibration residual: 0.617 mV

### VOUT measurement and CV output path

| Requested VOUT | Mean raw code | DMM VOUT | ADC fit residual |
|---:|---:|---:|---:|
| 1.000 V | 1179.8 | 0.9992 V | -0.516 mV |
| 3.000 V | 3543.0 | 2.979 V | -0.044 mV |
| 5.000 V | 5905.8 | 4.959 V | -0.106 mV |
| 10.000 V | 11815.0 | 9.911 V | -0.419 mV |
| 15.000 V | 17725.5 | 14.863 V | +0.358 mV |

The unrestricted ADC fit is:

`VOUT = 0.000837929890 * raw + 0.010462092 V`

Firmware retains the integer zero correction of -12 codes and uses:

- `MCP3464_VOUT_ZERO_RAW = -12`
- `MCP3464_VOUT_GAIN_PPM = 1004656`
- maximum calibration residual: 0.516 mV

The complete DAC-to-output fit is:

`VOUT = 0.990295342 * requested_voltage + 0.008231677 V`

Its R-squared value is 0.999999991 and the maximum non-linearity of the five
points relative to the fitted line is 0.708 mV. The firmware applies the
inverse fit using `CV_OUTPUT_GAIN_PPM = 990295` and
`CV_OUTPUT_OFFSET_UV = 8232` before calculating the DAC8562 code.

### Post-flash verification

The calibrated firmware was flashed and verified on the same board. VIN was
left at 20.000 V and the output was unloaded with a 100 mA current limit.

| Requested VOUT | DAC code | Firmware ADC | DMM | Output error |
|---:|---:|---:|---:|---:|
| 5.000 V | 11975 | 4.998-4.999 V | 4.999 V | -1 mV |
| 10.000 V | 23970 | 10.001 V | 10.000 V | 0 mV |
| 15.000 V | 35964 | 15.000-15.001 V | 15.001 V | +1 mV |

The maximum observed output error after calibration was 1 mV. At the original
20.000 V VIN calibration point, the updated firmware reported 20.000 V for raw
code 23781. The output was switched off after verification.

## Full-range 100-point sweep

The final sweep covers DAC codes 0 through 65535, corresponding nominally to
0 through 3.000 V. Digital GAINCAL prevents the signed 16-bit ADC output from
saturating near the positive rail. At 3.000 V, CC reads 32721 and CV reads
32547, both below the signed maximum of 32767.

After the final endpoint calibration:

- CC maximum absolute internal loopback error: 0.326 mV
- CC RMS error: 0.196 mV
- CV maximum absolute internal loopback error: 0.738 mV
- CV RMS error: 0.360 mV
- CC maximum error relative to 3 V full scale: 0.0109%
- CV maximum error relative to 3 V full scale: 0.0246%

All 100 raw and converted results are in
`docs/dac_adc_sweep_100_points.csv`.

## Schematic conversion factors

Nominal MCP3464 input resolution at gain x1 and 3.000 V reference:

`3.000 V / 32768 = 91.5527 uV per ADC code`

VIN and VOUT use matched 160 kohm input and 17.4 kohm feedback networks:

`Vadc = (17.4 kohm / 160 kohm) * Vsense = 0.10875 * Vsense`

Consequently:

`Vsense = ADC_code * 0.841864 mV`, before offset and gain correction.

R69 is a 50 milliohm Kelvin shunt. The assembled board has R87 fitted and R84
not fitted, so U18 has gain 10:

`Vadc = Iout * 50 milliohm * 10`

`Iout = ADC_code * 0.183105 mA`, before offset and gain correction.

Important assembly condition:

- R84 only: current gain is 11 and the nominal factor is 0.166460 mA/code.
- R87 only: current gain is 10 and the nominal factor is 0.183105 mA/code;
  this is the confirmed assembled-board configuration and firmware setting.
- R84 and R87 together: R69 is bypassed through RGND and current measurement
  is invalid.

The generated BOM lists both R84 and R87 in the zero-ohm resistor group even
though the schematic says to fit only one. The physical board must be checked
before enabling the power stage. Current readings are also invalid until the
missing U18 amplifier is populated.
