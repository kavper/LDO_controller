# G4 firmware brief: talk to the LDO G0 over UART

This file is for the **G4 / DCDC** CubeMX+firmware chat (`Digital_PSU_G474RCT` and the new shared-board schematic). The G0 LDO side is already matched to the 2026-08-30 schematics in `kavper/LDO_controller` (branch `cursor/g0-hw-rev-cubemx-19b5`, PR on that repo).

Do **not** copy the old G4 USART2 mapping (PB3/PB4). That was the previous board. The new common PCB uses different G4 pins.

## Schematic: what is OK vs what is not

UART isolator (G0 `U26` ISO6721RBDR) is electrically correct if G4 drives the nets as below. Channel directions:

| Isolator pin | Net | Direction |
|---|---|---|
| 2 INA (G4 side) | `USART2_TX_G0` | **G4 TX → G0 RX** |
| 7 OUTA (G0 side) | `STM_RX_G0` | into G0 **PA3** USART2_RX |
| 6 INB (G0 side) | `STM_TX_G0` | from G0 **PA2** USART2_TX |
| 3 OUTB (G4 side) | `USART2_RX_G0` | **G0 TX → G4 RX** |

The name `USART2_*_G0` is from the **G0** USART2, not from the G4 peripheral. On G474, **PB14/PB15 are USART3**, not USART2.

G4 schematic MCU sheet: `USART2_TX_G0` on **PB14**, `USART2_RX_G0` on **PB15**, header J6.

**G4 CubeMX must be:**

- Peripheral: **USART3** (AF7 on PB14/PB15), **not** USART2
- PB14 = USART3_TX, label `USART2_TX_G0`
- PB15 = USART3_RX, label `USART2_RX_G0`
- 115200 8N1, no flow control
- NVIC USART3 RX interrupt (or DMA) so you do not poll-block

Old `Digital_PSU_G474RCT.ioc` still has USART2 on **PB3/PB4**. That will **not** reach the isolator on the new PCB. Move it.

`POWER_PERMIT_G4` (G0 opto U28) is independent of UART: when G4 asserts it, G0 `POWER_KILL` goes low and the analog `OUT OFF` OR-gate kills the LDO FET even if firmware is wedged. Wire that GPIO on G4 and document polarity from the G4 schematic.

Hardware gaps on G0 (do not expect G0 GPIO for these): `BLEED_ON` and `REMOTE_ON` are **not** connected to the G0 MCU on this revision.

## How G0 speaks today

G0 USART2 is already 115200 8N1 on PA2/PA3.

Bring-up **stage 6** (current `APP_BRINGUP_STAGE`) uses the **text console**, not the binary frames:

```
SET V=5.000 I=0.100
OUT ON
OUT OFF
STATUS
HELP
```

Lines end with CR, LF or CRLF. Answers are `ACK` / `NACK`. STATUS is also pushed once per second.

For a product link, G0 also has a binary protocol (`UART_Protocol_Init`, `APP_BRINGUP_STAGE 0`):

- Frame: `A5 5A LEN TYPE SEQ PAYLOAD CRC16_LE`
- LEN = TYPE + SEQ + PAYLOAD
- CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`) over LEN..PAYLOAD
- Multi-byte fields little-endian
- Host→G0: `0x01` SET_VOLTAGE (u32 mV), `0x02` SET_CURRENT (u32 mA), `0x03` SET_OUTPUT (u8 0/1), `0x04` PING
- G0→host: `0x80` TELEMETRY, `0x81` ACK (payload = type), `0x82` NACK

Until G4 firmware is ready, leave G0 on stage 6 and type ASCII from a USB-UART on J6, or switch G0 to stage 0 when G4 implements the binary client.

## G4 implementation checklist

1. Open G4 `.ioc`, **unassign USART2 from PB3/PB4** (those pins are used elsewhere on the new MCU sheet).
2. Assign **USART3** TX=PB14, RX=PB15, 115200 8N1, interrupt enabled.
3. Labels must match the schematic nets so the next CubeMX regen stays readable.
4. First smoke test: send `PING` binary or `STATUS\r\n` ASCII and expect a reply on the isolator. If nothing comes back, swap is almost always TX/RX or USART2-vs-USART3.
5. `POWER_PERMIT_G4`: only assert when the DCDC rail is actually allowed to feed the LDO.
6. Do not talk to G0 fan PWM/TACH — fan moved to G4 on this board.

G0 PR with matching CubeMX: repository `kavper/LDO_controller`, branch `cursor/g0-hw-rev-cubemx-19b5`.
