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
- `OUT ON` enables `STM_OUT_OFF` only after PGOOD, POWER_KILL (must be low), VIN,
  VOUT≈0 and temperature checks pass.
- `OUT OFF` asserts analog off immediately; DAC ramps to zero; `bleed` requests
  discharge if VOUT is still up.

Aliases: `ON` / `OUTPUT ON` / `OUT=ON` and the OFF equivalents.

## Telemetry

Every 200 ms:

```text
TLM out=0 mode=0 vset=0 vout=8 iset=0 iout=0 vin=5980 t1=3475 t2=3571 t3=3484 t4=3161 bleed=0 fan=54 pgood=1 kill=1 cccv=0 fault=NONE
```

See `docs/G4_LDO_UART.md`. G4 must copy `bleed` and `fan` to its GPIOs.

## Protection (output forced OFF)

POWER_KILL high, PGOOD lost, VIN low, overtemperature, VOUT overshoot, DAC
readback error. `kill=1` means G4 is not permitting; analog FETs stay off
regardless of UART.
