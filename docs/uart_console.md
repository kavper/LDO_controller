# Interactive UART console

Stage 6 provides a human-readable console on USART2 at 115200 baud, 8 data
bits, no parity and one stop bit. End every command with CR, LF or CRLF.

## Commands

```text
SET V=5.000 I=0.100
OUT ON
OUT OFF
STATUS
HELP
```

- `SET` accepts volts and amperes with up to three decimal places. It updates
  the requested voltage and analog current limit but does not enable the output.
- `OUT ON` enables the output only after the ADC/DAC, PGOOD, VIN, VOUT and
  temperature preflight checks pass.
- `OUT OFF` disables the power stage immediately; the DAC setpoints then ramp
  down to zero and the bleeder discharges the output.
- `STATUS` prints the same engineering-unit table that is sent automatically
  once per second.
- Every valid command returns `ACK`; malformed, out-of-range or unsafe commands
  return `NACK` with a reason.

The accepted ranges are 0.000 to 27.000 V and 0.000 to 5.000 A. The voltage
limit reflects the 160 kOhm / 17.4 kOhm CV network and the 3.000 V DAC
reference. Current-limit scaling uses the assembled R87 path, the 50 mOhm
shunt and the U17A gain of 11.

## Startup and protection

After every reset the output starts disabled and both DAC channels are zero.
Setpoints are not retained across reset. While the output is enabled, the
console forces it off on:

- ADC or DAC initialization failure
- deasserted 5 V PGOOD
- VIN below 6 V
- any temperature at or above 60 C
- VOUT more than 0.75 V above the requested value
- DAC CV or CC readback error greater than 30 mV after settling

The `IOUT` field intentionally reports `N/A (no U18)`. The analog current-limit
loop is active, but actual output-current telemetry cannot be trusted until U18
is populated.
