# G0 host UART

USART2, 115200 8N1, CR/LF. Production interface is `TLM` plus three commands.
There is no STATUS table.

## Commands

```text
SET V=5.000 I=0.100
OUT ON
OUT OFF
```

- `SET` updates voltage and current-limit setpoints. Does not enable the output.
  Analog CV holds `V=`; analog CC holds `I=`. `mode` / `cccv` only report which
  loop is winning. Hitting the current limit does **not** turn the output off.
- `OUT ON` enables `STM_OUT_OFF` only after PGOOD, POWER_KILL (must be **low**), VIN,
  VOUT≈0 and temperature checks pass.
- `OUT OFF` asserts analog off immediately; DAC ramps to zero; `bleed` requests
  discharge if VOUT is still up.

Aliases: `ON` / `OUTPUT ON` / `OUT=ON` and the OFF equivalents.

## Telemetry

Every 200 ms:

```text
TLM out=0 mode=0 vset=0 vout=8 iset=0 iout=0 vin=5980 t1=3475 t2=3571 t3=3484 t4=3161 bleed=0 fan=54 pgood=1 kill=1 cccv=0 fault=NONE
```

Full G4 paste-spec: `docs/G4_G0_UART_PROTOCOL.md`. Pins/policy: `docs/G4_LDO_UART.md`. G4 must copy `bleed` and `fan` to its GPIOs.

## Protection (output forced OFF)

POWER_KILL high (debounced), PGOOD lost, VIN low, overtemperature, VOUT
overshoot above the CV setpoint. Current limit and DAC readback are **not**
trips. `kill=1` means `POWER_KILL` is high (analog FETs held off).
G4 must drive `POWER_PERMIT_G4` **PB6 high** (or jumper G0 PB1 to GND) before
`OUT ON` can produce VOUT. See `docs/G4_LDO_UART.md`.
