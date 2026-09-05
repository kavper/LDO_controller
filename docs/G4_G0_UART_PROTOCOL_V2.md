# G4 ↔ G0 reliable UART protocol v2

This is the production interface between G4 and G0. Both firmwares must be
flashed together. The H7/PC host UART remains a separate ASCII interface.

## Physical layer

- ISO6721, about 2 cm PCB route.
- USART2, 115200 baud, 8N1, no flow control.
- A 13-byte command takes about 1.2 ms and the 75-byte telemetry frame about
  6.6 ms, so this baud is sufficient for a 50 ms sequencer.
- Increase baud only as one coordinated G4+G0 change.

## Frame

All multi-byte integers are little-endian.

```
A5 5A LEN TYPE SEQ PAYLOAD... CRC_LO CRC_HI
```

`LEN` counts `TYPE + SEQ + PAYLOAD`. CRC is CRC-16/CCITT-FALSE
(polynomial `0x1021`, initial value `0xFFFF`) over `LEN` through the last
payload byte. A bad CRC or incomplete frame has no effect on outputs.

| Type | Direction | Payload |
|---|---|---|
| `0x03` SET_OUTPUT | G4→G0 | `uint8`, 0 or 1 |
| `0x04` PING | G4→G0 | empty |
| `0x05` SETPOINT | G4→G0 | `uint32 mV`, `uint32 mA` atomically |
| `0x80` TELEMETRY | G0→G4 | layout below |
| `0x81` ACK | G0→G4 | acknowledged TYPE |
| `0x82` NACK | G0→G4 | rejected TYPE, reason |

NACK reasons: `1` unknown type, `2` bad payload, `3` range, `4` unsafe.
Voltage range is 0..27000 mV and current range is 0..5000 mA. SETPOINT works
while output is ON. SET_OUTPUT=1 runs G0 preflight; SET_OUTPUT=0 always works.

G4 accepts ACK/NACK only when both `SEQ` and acknowledged `TYPE` match its
pending command. Timeout is 150 ms with up to four attempts. Commands are
idempotent, so a lost ACK is safe.

## Telemetry payload (`0x80`, 68 bytes, every 100 ms)

| Offset | Type | Meaning |
|---:|---|---|
| 0 | u32 | vout_mV |
| 4 | u32 | iout_mA |
| 8 | u32 | vin_mV |
| 12 | u32 | DAC CV readback mV |
| 16 | u32 | DAC CC readback mV |
| 20 | u32 | voltage target mV |
| 24 | u32 | current target mA |
| 28 | u32 | pre-regulator request mV |
| 32..35 | 4×u8 | mode, output enabled, bleed request, PGOOD |
| 36 | u32 | fault flags |
| 40..47 | 4×u16 | raw temperature ADC |
| 48..55 | 4×u16 | filtered temperature ADC |
| 56..63 | 4×i16 | temperatures centi-°C (`INT16_MIN` invalid) |
| 64..67 | 4×u8 | fan %, power kill, CC/CV, raw OUT_OFF |

If telemetry is stale for more than 500 ms, G4 marks the link stale and uses
40% fan failsafe. G4 never forwards binary G0 traffic to the host UART.

Fault bits: bit 0 `HW_INIT`, 1 `PGOOD_LOST`, 2 `POWER_KILL`, 3 `VIN_LOW`,
4 `VOUT_HARD`, 5 `VOUT_HIGH`, 6 `TEMP_HIGH`, 7 `IOUT_HARD`. `IOUT_HARD`
is latched after the G0 final-output measurement reaches 5500 mA for 10 ms;
G0 asserts its local `OUT_OFF` and immediately queues fault telemetry. G4 may
recover from `VIN_LOW` without killing the pre-regulator; the other runtime
faults remove power permit and force a hard stop of both G0 and the DCDC stage.

## Update and verification

Flash both v2 firmwares with the power output physically disabled, power-cycle
both MCUs, and confirm host `T` shows increasing `g0_tlm`, `g0_err=0`, and
`g0_age_ms < 500`. Old ASCII and v2 binary firmwares are incompatible.
