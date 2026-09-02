# G4 firmware brief: LDO G0 link, fan, bleeder, H7/debug

For the **G4 / DCDC** CubeMX+firmware chat (`Digital_PSU_G474RCT`, new shared PCB dated 2026-08-30). G0 LDO firmware lives in `kavper/LDO_controller` branch `cursor/g0-hw-rev-cubemx-19b5`.

Do **not** reuse the old G4 USART2 mapping (PB3/PB4). That board is gone.

## Roles on the new board

| MCU | Job |
|---|---|
| **G0** | LDO: DAC setpoints, MCP3464 VIN/VOUT/IOUT, 4× NTC, OUT_OFF, CC/CV sense. **No fan GPIO, no BLEED_ON GPIO.** Computes `bleed` + `fan` requests and streams telemetry. |
| **G4** | DCDC + **owns the fan** + **drives BLEED_ON / REMOTE_ON / POWER_PERMIT_G4**. Talks to G0 over isolated UART. Forwards G0 telemetry to H7, or until H7 is ready **prints the same line on a debug UART to a PC**. |
| **H7** | Later: UI / PD / logging. Same payload G4 already parsed. |

```
PC debug UART  <── G4 USART? (until H7 exists)
H7 USART1      <── G4 PC4 TX / PC5 RX  (nets USART1_TX_H7 / USART1_RX_H7)
G0 USART2      <── isolator ──> G4 PB14 TX / PB15 RX  (nets USART2_*_G0, use USART3)
```

## CubeMX pinout G4 (new schematic)

### UART to G0 (isolator, net names are from G0)

ISO6721RBDR on the G0 sheet:

| Isolator | Net | Meaning |
|---|---|---|
| INA pin 2 | `USART2_TX_G0` | **G4 TX → G0 RX** |
| OUTA pin 7 | `STM_RX_G0` | G0 PA3 USART2_RX |
| INB pin 6 | `STM_TX_G0` | G0 PA2 USART2_TX |
| OUTB pin 3 | `USART2_RX_G0` | **G0 TX → G4 RX** |

On G474 **PB14/PB15 = USART3 AF7**, not USART2.

- USART3 TX = **PB14** label `USART2_TX_G0`
- USART3 RX = **PB15** label `USART2_RX_G0`
- 115200 8N1, RX interrupt or DMA
- Header J6 is the same nets (handy for a USB-UART sniffer)

### UART to H7

- USART1 TX = **PC4** `USART1_TX_H7`
- USART1 RX = **PC5** `USART1_RX_H7`
- Same 115200 8N1. Until H7 firmware exists, G4 should also dump every G0 `TLM` line on whatever UART is wired to a computer (ST-LINK VCP or a spare USART). **Do not invent a second protocol** — forward the G0 line as-is.

### Fan (G4 only)

- **PA6** `FAN_PWM` (TIM PWM, transistor Q9)
- **PA5** `FAN_TACH` (input capture)

Duty comes from G0 field `fan` (0..100 %). Apply it to FAN_PWM. TACH is local to G4 (stall detect); G0 does not have tach.

Suggested local failsafe if G0 telemetry is older than 500 ms: hold last duty, or 40 % failsafe, never 0 % if MOSFETs were recently hot.

### Actuators G4 must drive (not on G0 MCU)

| G4 pin | Net | What G0 asks |
|---|---|---|
| **PB4** | `BLEED_ON` | `bleed=1` → turn bleeder MOSFET on |
| **PB5** | `REMOTE_ON` | not requested by G0 yet; leave off unless UI asks |
| **PB6** | `POWER_PERMIT_G4` | Light the G0 opto so `POWER_KILL` goes **low** (permit). High/Hi-Z = analog kill. |

- `BLEED_ON` high → Q13 N-FET on.
- `STM_OUT_OFF` **high** or `POWER_KILL` **high** → analog FETs off.
- `POWER_KILL` **high** = kill (`kill=1`). G4 must pull it **low** for VOUT.
- `STM_CC_CV` high = CC (DS2 on). G0 DS3 is active-low.

## What G0 measures and why G4 cares

NTC on G0 ADC1 (centi-degC in telemetry, `t1`…`t4`):

| Field | Sensor | Fan? |
|---|---|---|
| t1 | MOSFET | yes (hottest of t1/t3/t4) |
| t2 | ambient | advisory (+5 °C vs hottest) |
| t3 | bleeder resistor | yes |
| t4 | 3V3 / 15-to-5 area | yes |

Invalid temperature is `INT32_MIN` in ASCII (`-2147483648`) or `INT16_MIN` in binary.

G0 also reports VIN, VOUT, IOUT, DAC readbacks, CC/CV, PGOOD, POWER_KILL.

## Bleeder policy (G0 computes, G4 drives PB4)

G0 **cannot** toggle `BLEED_ON`. It sets `bleed` in telemetry. G4 copies that bit to PB4.

Rules now in G0 (`bleeder.c`):

- Output **ON** and **setpoint** `< 4.000 V` → bleed ON (minimum load).
- Output **ON** and setpoint `≥ 4.200 V` → bleed OFF (0.2 V hysteresis).
- Output **OFF** and VOUT `> 0.500 V` → bleed ON (discharge).
- Output **OFF** and VOUT `< 0.200 V` for 500 ms → bleed OFF.

Do **not** re-implement a different curve on G4 unless the G0 flag is missing.

## Fan policy (G0 computes percent, G4 PWMs PA6)

`fan` is 0..100. Curve on G0 (`fan_request.c`):

- hottest of MOSFET / bleeder / PSU NTC
- `≤ 30.00 °C` → 20 % (keep spinning)
- `30.00 … 55.00 °C` → linear 20…100 %
- `≥ 55.00 °C` → 100 %
- no valid NTC → 40 % failsafe

G4 should **not** ignore G0 and run its own thermistors unless they are extra DCDC sensors; LDO heat is on G0.

## G0 → G4 feedback (implement this first)

G0 USART2 115200 8N1. Production firmware speaks ASCII `TLM` plus `SET` / `OUT`.

### ASCII machine line (every 200 ms)

One line G4 must parse and **forward unchanged** to H7 or the PC:

```
TLM out=0 mode=1 vset=4000 vout=3990 iset=100 iout=0 vin=12000 t1=3250 t2=2510 t3=2600 t4=2800 bleed=1 fan=35 pgood=1 kill=0 cccv=0 fault=NONE
```

| Token | Unit / meaning |
|---|---|
| out | 0 off, 1 on |
| mode | 0 OFF, 1 CV, 2 CC |
| vset / vout / vin | millivolts |
| iset / iout | milliamps |
| t1..t4 | centi-degC (3250 = 32.50 °C) |
| bleed | 0/1 → G4 `BLEED_ON` |
| fan | 0..100 → G4 `FAN_PWM` duty |
| pgood | G0 5 V buck PGOOD |
| kill | 1 = analog kill (`POWER_KILL` **high**; G4 not permitting) |
| cccv | 1 = CC (`STM_CC_CV` high, DS2 on) |
| fault | `NONE` or console fault name |

Host→G0 ASCII (CR/LF): `SET V=5.000 I=0.100`, `OUT ON`, `OUT OFF`.

## G4 software loop (what to write)

```
every UART byte from USART3:
  reassemble TLM line
  copy line to USART1 (H7) and/or debug UART

every TLM / telemetry:
  FAN_PWM duty = fan
  BLEED_ON = bleed
  optionally log TACH RPM on debug UART

POWER_PERMIT_G4:
  pull G0 POWER_KILL **low** (permit / analog live)
  release = POWER_KILL high = analog kill, independent of UART
```

First smoke test: USB-UART on J6, 115200. Idle pull-up → `kill=1`. Bench without G4: jumper `POWER_KILL` to GND, then `SET` / `OUT ON`.

## Hardware gaps (do not fight the PCB)

- G0 has no `BLEED_ON` / `REMOTE_ON` / fan pins. G4 must drive them.
- Fan PWM/TACH live only on G4 PA6/PA5.
- Old G4 `.ioc` USART2 PB3/PB4 will not hit the isolator.

G0 PR: `kavper/LDO_controller` / `cursor/g0-hw-rev-cubemx-19b5`.
