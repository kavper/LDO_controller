# OBSOLETE: G4 ↔ G0 ASCII UART protocol v1

> Do not implement this version. Production firmware now uses binary v2 with
> CRC, sequence matching and atomic setpoints. See
> [`G4_G0_UART_PROTOCOL_V2.md`](G4_G0_UART_PROTOCOL_V2.md). This file remains
> only as historical bring-up documentation.

This is the **production** interface on the shared PCB (2026-08-30).
G0 firmware: `kavper/LDO_controller` branch `cursor/g0-hw-rev-cubemx-19b5`.
G4 repo: `kavper/Digital_PSU_G474RCT`.

The instructions below describe the retired ASCII implementation.

Do **not** invent a second protocol for H7 or the PC: parse TLM for local GPIO, then **forward the same ASCII line unchanged**.

---

## 1. Physical link

| Item | Value |
|---|---|
| Isolator | ISO6721: G4 TX → G0 RX, G0 TX → G4 RX |
| G0 MCU | USART2 **PA2 TX / PA3 RX** |
| G4 MCU | **USART3 PB14 TX / PB15 RX**, AF7 |
| Schematic net names | `USART2_TX_G0` / `USART2_RX_G0` (named from G0 — **not** G4 USART2) |
| **Wrong** (old board) | G4 USART2 PB3/PB4 — does **not** hit the isolator |
| Baud | **115200 8N1**, no flow control |
| Sniffer | Header **J6** is the same nets |

CubeMX G4: USART3 asynchronous, 115200, 8N1, RX interrupt or DMA idle-line.

---

## 2. Line format

- Encoding: ASCII.
- Line end: **CR, LF, or CR+LF**. G0 treats `\r` or `\n` as end-of-line; empty lines are ignored.
- G0 **uppercases** incoming commands and collapses whitespace. `set v=5.0 i=0.1` is OK.
- G0 incoming command max **95** printable characters.
- G0 outgoing TLM is one line, typically **< 220** characters. Give G4 RX ≥ **256** bytes.
- G0 TX queue is only **4** lines deep. Do not flood commands; wait for ACK/NACK (TLM still arrives in between).

G4 RX parser: accumulate bytes until `\n` or `\r`, strip the other, then classify the line by prefix (`TLM`, `ACK`, `NACK`).

---

## 3. Traffic model

```
G0  ── TLM every 200 ms (unsolicited) ──►  G4
G0  ── ACK / NACK (after a command, or on fault) ──►  G4
G4  ── SET / OUT ON / OUT OFF ──►  G0
```

- G0 **never** enables the output by itself. Boot: output off, `STM_OUT_OFF` high.
- `SET` only stores setpoints. It does **not** turn the output on.
- TLM and ACK share the same UART. G4 must handle a stream like:

```
TLM ...
ACK SET V=5.000 I=0.100 OUT=OFF
TLM ...
NACK OUT ON REASON=POWER_KILL
TLM ...
```

Do not block the TLM parser while waiting for ACK. Match ACK to the last command by prefix, with a timeout (~200–500 ms).

---

## 4. G4 → G0 commands (exactly these)

Send with `\r\n`. Preferred forms:

```
SET V=5.000 I=0.100
OUT ON
OUT OFF
```

### `SET V=<volts> I=<amps>`

| Rule | Detail |
|---|---|
| Order | **`V=` then `I=`**. `SET I=… V=…` is rejected (`NACK UNKNOWN`). |
| Volts / amps | Decimal, up to **3** fraction digits. `V=5`, `V=5.0`, `V=5.000` all work. |
| Optional suffix | `V` after voltage, `A` after current: `SET V=5.000V I=0.100A` |
| Range | V **0.000 … 27.000**, I **0.000 … 5.000** |
| Effect | Updates CV/CC DAC targets. Output state unchanged. |

Success:

```
ACK SET V=5.000 I=0.100 OUT=OFF
```

(`OUT=ON` if the output was already enabled.)

Out of range:

```
NACK RANGE V=0.000..27.000V I=0.000..5.000A
```

### `OUT ON`

Aliases (same after uppercase): `OUTPUT ON`, `OUT=ON`, `ON`.

Preflight on G0 (any fail → no enable):

| `REASON=` | Meaning | What G4 should do |
|---|---|---|
| `ADC_OR_DAC_INIT` | MCP3464 or DAC8562 failed at boot | Hardware / SPI, not UART |
| `PGOOD_5V` | G0 5 V buck `PGOOD` not high | Wait for G0 rail |
| `POWER_KILL` | `POWER_KILL` still **high** | Drive **PB6 `POWER_PERMIT_G4` HIGH**, wait TLM `kill=0`, retry |
| `VIN_LOW` | VIN < **4500 mV** | Raise DCDC / wait for VIN |
| `VOUT_NOT_ZERO` | VOUT > **250 mV** | Wait bleed-down (`bleed=1`), then retry |
| `TEMPERATURE` | any NTC invalid or ≥ **60.00 °C** | Cool down / check sensors |

Success (idempotent if already on):

```
ACK OUT ON
```

Fail:

```
NACK OUT ON REASON=POWER_KILL
```

### `OUT OFF`

Aliases: `OUTPUT OFF`, `OUT=OFF`, `OFF`.

Always succeeds:

```
ACK OUT OFF
```

G0 asserts analog off immediately; DAC ramps to 0; TLM `bleed` may go 1 to discharge VOUT.

### Anything else

```
NACK UNKNOWN; USE SET V=.. I=.. | OUT ON | OUT OFF
```

There is **no** `HELP`, `STATUS`, `GET`, or binary PING on the production interface.

---

## 5. G0 → G4 telemetry (`TLM`, every 200 ms)

Exact format (one space between tokens, `\r\n` at end). Token **order is stable** in current firmware; still parse by **key=value**, ignore unknown keys if a future field is added.

```
TLM out=0 mode=0 vset=0 vout=8 iset=0 iout=0 vin=9600 t1=3250 t2=2510 t3=2600 t4=2800 bleed=0 fan=20 pgood=1 kill=1 outoff=1 cccv=0 fault=NONE
```

| Token | Type | Unit / meaning |
|---|---|---|
| `out` | 0/1 | Software output enable (1 = G0 released analog off **in software**) |
| `mode` | 0/1/2 | `0` OFF, `1` CV, `2` CC. **Status only** — not a fault |
| `vset` | uint | Voltage **setpoint mV** |
| `vout` | uint | Measured VOUT **mV** |
| `iset` | uint | Current-limit **setpoint mA** (analog CC holds this) |
| `iout` | uint | Measured IOUT **mA** |
| `vin` | uint | Measured VIN **mV** |
| `t1` | int | MOSFET NTC, **centi-°C** (`3250` = 32.50 °C) |
| `t2` | int | Ambient NTC, centi-°C |
| `t3` | int | Bleeder NTC, centi-°C |
| `t4` | int | PSU-area NTC, centi-°C |
| `bleed` | 0/1 | **Copy to G4 PB4 `BLEED_ON`** (high = on) |
| `fan` | 0…100 | **Copy to G4 PA6 `FAN_PWM` duty %** |
| `pgood` | 0/1 | G0 5 V buck PGOOD high |
| `kill` | 0/1 | `1` = `POWER_KILL` **high** = analog FETs held off |
| `outoff` | 0/1 | Raw PA15 `STM_OUT_OFF` (`1` = high = analog off request) |
| `cccv` | 0/1 | `1` = analog current limit (`STM_CC_CV` high). **Do not kill DCDC or PB6.** |
| `fault` | string | `NONE` or last fault name (no spaces) |

Invalid temperature: `-2147483648` (`INT32_MIN`). Treat as missing; do not use for fan.

### How to read `out` / `outoff` / `kill` together

Analog FET gate is a **diode-OR**: `POWER_KILL` high **or** `STM_OUT_OFF` high → FETs **off**. VOUT can be 0 even if `out=1`.

| `out` | `outoff` | `kill` | Meaning |
|---|---|---|---|
| 0 | 1 | 1 | Idle: software off, analog killed by both |
| 1 | 0 | 1 | Software ON, PA15 released, **still killed by POWER_KILL** → VOUT stays 0 |
| 1 | 0 | 0 | Software ON and analog permitted → VOUT should rise after SET |
| 0 | 1 | 0 | Software OFF, permit present, analog off from G0 |

G4 must **permit first** (`kill=0`), then `OUT ON`. UART `OUT ON` cannot override a high `POWER_KILL`.

---

## 6. Unsolicited fault (output was ON)

G0 forces output OFF and sends:

```
NACK FAULT=POWER_KILL; OUTPUT FORCED OFF
```

`FAULT=` names: `HW_INIT`, `PGOOD_LOST`, `POWER_KILL`, `VIN_LOW`, `VOUT_HARD`, `VOUT_HIGH`, `TEMP_HIGH`.

**Current limit is not a fault.** Analog CC holds `iset` and lets `vout` fall below `vset`. TLM `mode=2` / `cccv=1` is informational. G4 must **not** send `OUT OFF`, drop PB6, or fold the DCDC voltage because of CC.

On `POWER_KILL` / DCDC collapse: G4 must also drop **PB6 immediately** (hardware kill). Do not wait for UART.

---

## 7. GPIO that is **not** UART (G4 must still do this)

UART never toggles these. TLM only **requests** bleed/fan.

| G4 pin | Net | CubeMX | Drive |
|---|---|---|---|
| **PB6** | `POWER_PERMIT_G4` | GPIO PP, reset **Low**, no pull | **HIGH** = permit (opto on → G0 `POWER_KILL` low). **LOW** = kill |
| **PB4** | `BLEED_ON` | GPIO PP, reset Low | `bleed=1` → HIGH |
| **PB5** | `REMOTE_ON` | GPIO PP, reset Low | leave LOW unless UI asks |
| **PA6** | `FAN_PWM` | TIM PWM | duty = `fan` 0…100 |
| **PA5** | `FAN_TACH` | input capture | local stall detect; G0 has no tach |

PB6 HAL:

```c
HAL_GPIO_WritePin(POWER_PERMIT_G4_GPIO_Port, POWER_PERMIT_G4_Pin, GPIO_PIN_SET);   /* permit */
HAL_GPIO_WritePin(POWER_PERMIT_G4_GPIO_Port, POWER_PERMIT_G4_Pin, GPIO_PIN_RESET); /* kill */
```

If TLM is older than **500 ms**: keep last `fan` or use **40 %** failsafe; do not drop to 0 % if the LDO was recently hot.

Do **not** recompute bleeder or LDO fan curves on G4. Copy `bleed` and `fan`.

---

## 8. Bring-up sequence G4 must implement

```
boot:
  PB6 = 0, PB4 = 0, PB5 = 0
  start USART3 115200
  parse TLM (forward to H7 / debug UART)

when DCDC is in regulation:
  PB6 = 1
  wait until TLM kill=0 (timeout ~200–500 ms, else DCDC/opto fault)
  wait until TLM pgood=1 and vin>=4500
  send: SET V=<volts> I=<amps>\r\n
  wait ACK SET
  if TLM vout > 250: wait for bleed (or send OUT OFF and wait vout≈0)
  send: OUT ON\r\n
  wait ACK OUT ON
  expect TLM: out=1 outoff=0 kill=0, then vout tracking vset

UI / host wants off:
  send OUT OFF
  PB6 may stay 1 (permit) or go 0 (hard kill) — product choice
  on DCDC fault / E-stop: PB6 = 0 first, then optional OUT OFF
```

H7 later: G4 USART1 **PC4 TX / PC5 RX**, same 115200, forward TLM lines as-is. Until then, dump TLM on ST-LINK VCP or any debug UART.

---

## 9. Example session (J6 sniffer or G4 log)

Idle (no G4 permit):

```
TLM out=0 mode=0 vset=0 vout=0 iset=0 iout=1 vin=9600 t1=2800 t2=3000 t3=2900 t4=3100 bleed=0 fan=20 pgood=1 kill=1 outoff=1 cccv=0 fault=NONE
```

G4 sets PB6 high, then:

```
G4: SET V=5.000 I=0.100\r\n
G0: ACK SET V=5.000 I=0.100 OUT=OFF\r\n
G4: OUT ON\r\n
G0: ACK OUT ON\r\n
G0: TLM out=1 mode=1 vset=5000 vout=4990 iset=100 iout=20 vin=9600 ... kill=0 outoff=0 cccv=0 fault=NONE
```

Load steps into analog CC (output stays **on**; G4 must not trip):

```
G0: TLM out=1 mode=2 vset=5000 vout=1800 iset=100 iout=100 vin=9600 ... cccv=1 fault=NONE
```

If G4 skipped PB6:

```
G4: OUT ON\r\n
G0: NACK OUT ON REASON=POWER_KILL\r\n
```

---

## 10. Suggested G4 parse (C-like)

```c
/* After collecting one line without CR/LF: */
if (strncmp(line, "TLM ", 4) == 0) {
  /* sscanf or token walk: out=, bleed=, fan=, kill=, vout=, vin=, fault= */
  HAL_GPIO_WritePin(BLEED_ON_GPIO_Port, BLEED_ON_Pin,
                    bleed ? GPIO_PIN_SET : GPIO_PIN_RESET);
  FanPwm_SetPercent(fan);          /* 0..100 */
  UART_WriteLine(huart_h7_or_debug, line); /* plus '\n' */
} else if (strncmp(line, "ACK ", 4) == 0) {
  /* complete pending SET / OUT command */
} else if (strncmp(line, "NACK ", 5) == 0) {
  /* log; if REASON=POWER_KILL, check PB6; if FAULT=, output is already off */
}
```

To send:

```c
HAL_UART_Transmit(huart3, (uint8_t *)"SET V=5.000 I=0.100\r\n", 21, 10);
HAL_UART_Transmit(huart3, (uint8_t *)"OUT ON\r\n", 8, 10);
HAL_UART_Transmit(huart3, (uint8_t *)"OUT OFF\r\n", 9, 10);
```

---

## 11. Pin / protocol checklist for the G4 `.ioc` + firmware

- [ ] USART3 PB14 TX, PB15 RX, 115200 8N1 — **not** USART2 PB3/PB4
- [ ] PB6 `POWER_PERMIT_G4` PP, reset Low; HIGH = permit
- [ ] PB4 `BLEED_ON` follows TLM `bleed`
- [ ] PA6 PWM follows TLM `fan`
- [ ] Commands: only `SET V=… I=…`, `OUT ON`, `OUT OFF`
- [ ] Parse `TLM` every ~200 ms; forward unmodified
- [ ] Permit (`kill=0`) **before** `OUT ON`
- [ ] `mode=2` / `cccv=1` = analog CC; keep VIN at Vset+dropout, do not trip
- [ ] No binary protocol, no HELP/STATUS

G0 host notes: `docs/uart_console.md`. Hardware/CubeMX: `docs/G4_LDO_UART.md`.
