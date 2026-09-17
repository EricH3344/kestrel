# Kestrel STM32F767 handset ↔ ExpressLRS module — integration plan (handoff)

Written for: a fresh coding agent working in `C:\Users\erich\OneDrive\Documents\Kestrel Repo\kestrel\STM32`
(CubeIDE project `Kestrel_Gimbal_Test`, FreeRTOS/CMSIS-v2, STM32 HAL, USB CDC). Copy this file into
that repo (suggested: `STM32/ELRS_HANDOFF.md`) and point the agent at it.

> **Revision 2026‑09‑16 — bench‑validated.** The module firmware was exercised end to end
> (PC emulating the handset → Kestrel TX → air → Kestrel RX → Pixhawk 6C Mini TELEM2 → Mission
> Planner): RC, MAVLink both directions, param download, telemetry. Everything in this document
> now reflects what was *observed*, not just read from source. Changes from the previous revision:
> 1. **Baud: 921600, not 400000.** At 400000 the module clamps the packet rate to ≥2 ms and the
>    LR1121 900 MHz table has no 500 Hz entry, so it lands on **250 Hz LoRa — same as 115200**.
>    F1000 needs a baud with no clamp: 921600 (or higher). See *Physical*.
> 2. **`rf_Mode` in LINK_STATISTICS is the rate *enum*, not the table index.** F1000 8ch = 11,
>    200 Hz Full = 6, 250 Hz = 7. Phase D confirmation values were wrong.
> 3. **RSSI bytes are signed dBm** on this build (`0xF4` = −12 dBm), not a positive magnitude.
> 4. A working reference handset exists: `F:\ExpressLRS\src\python\test_tools\kestrel_handset_bridge.py`.
>    Use it as the oracle for framing, cadence and behaviour (new section *Bench reference*).

## Context

Kestrel is a mostly-autonomous drone. The ground side is a custom STM32F767VITx handset:

- **Tablet running Mission Planner** on USB OTG → STM32 **USB CDC** (raw MAVLink both ways)
- **2 gimbals (4 ADC axes) + 14 digital inputs** → RC channels
- **One UART** → the Kestrel **ExpressLRS 900 MHz TX module** (ESP32‑PICO‑D4 + LoRa1121, one UART only, no WiFi)

The ELRS TX firmware (working copy at `F:\ExpressLRS`, vendored copy at
`kestrel/eLRS Module/ExpressLRS/src`) has been modified so the **single module UART carries
everything as CRSF**: RC channels, parameter reads/writes (rate/power switching), link statistics,
and — via a new private frame type `0xAA` — **raw MAVLink tunnelled in both directions**. The module
boots in *Link Mode = MAVLink*, rate *F1000 8ch*, *100 mW*. The RX side auto‑switches its UART to
MAVLink @460800 to the flight controller (RC delivered as `RC_CHANNELS_OVERRIDE` @100 Hz).

The STM32 project already contains a good skeleton for this (CRSF framing, RC-priority uplink MUX,
USB→uplink task, host unit tests) but was written against an earlier "AirPort" design and has
**four hard mismatches with the module firmware** plus several unfinished pieces. This plan brings
the handset into agreement with the module and finishes the data paths. The intended outcome: sticks
and switches arrive at the FC at 125–250 Hz, Mission Planner on the tablet sees a normal MAVLink
link (~5 kB/s down / ~2.5 kB/s up), and a handset switch can move the link between F1000 and
200 Hz Full at runtime.

ELRS-side reference files (read-only, for protocol truth):
- `F:\ExpressLRS\src\include\crsf_protocol.h` — frame types, addresses, `crsfLinkStatistics_t` (line ~399)
- `F:\ExpressLRS\src\lib\Handset\CRSFHandset.cpp` — module's UART parser, autobaud list, `0xAA` intercept, out-FIFO pacing
- `F:\ExpressLRS\src\src\tx_main.cpp` — `HandsetMavlinkIn/Out` (~line 1096), boot defaults (~line 1459)
- `F:\ExpressLRS\src\lib\CrsfProtocol\CRSFEndpoint.cpp` — parameter entry chunk format (`sendParameter`, ~line 225)
- `F:\ExpressLRS\src\lib\tx-crsf\TXModuleParameters.cpp` — parameter list/order; Packet Rate value encoding (line ~831)
- `F:\ExpressLRS\src\src\common.cpp` lines 61–80 — LR1121 rate table (index → rate)

---

## The wire contract (authoritative)

### Physical
- UART, **8N1, 921600 baud, full duplex**. The module autobauds only from
  `{400000, 115200, 5250000, 3750000, 1870000, 921600, 2250000}` — **460800 will never lock**.
  The module UART is USART3 on the Kestrel PCB (APB1, 54 MHz): 921600 → BRR 58.5625, 0.05 % error.
- **The handset baud caps the air rate** (`CRSFHandset::getMinPacketInterval` →
  `get_elrs_HandsetRate_max`, which walks *down* the rate table until the packet interval is long
  enough):

  | handset baud | min packet interval | rate you actually get on the LR1121 900 table |
  |---|---|---|
  | 115200 | 4 ms | 250 Hz LoRa (idx 1) — **measured on the bench, `rf_Mode = 7`** |
  | 400000 | 2 ms | 250 Hz LoRa (idx 1) — there is no 500 Hz entry, so it falls through to 250 |
  | 921600 and up | none | F1000 8ch (idx 0) as configured |

  Do not use 400000: it costs a rate clamp and buys nothing over 115200.
- The module counts the handset as "connected" on the first CRC‑valid frame and starts a
  **1 s watchdog**: if no valid frame arrives for ~1 s it drops the connection and starts cycling
  bauds. Therefore **RC frames must never stop**, even with sticks idle.
- **The module does not radiate at all until a handset is connected** (`noCrossfire`, RF timer
  stopped). The RX then sees nothing and stays "disconnected". Bench‑confirmed: both modules sit
  blinking until the first valid RC frame; ~1.5 s after that (`awaitingModelId` timeout) the TX
  starts transmitting and the RX locks within a couple of seconds.
- **Downlink is only flushed after the module receives a frame** from the handset (up to 128 bytes
  per received frame). Steady RC cadence = steady downlink.
- **Silence diagnosis:** if the module sends *nothing* back (not even CRC‑failed junk), it is not
  decoding your frames — 99 % of the time that is the baud. Once it decodes one frame it sends
  `0x3A` every 200 ms and `0x14` every ~250 ms, unconditionally.

### Frame layout (every frame, both directions)
```
[sync][len][type][payload …][crc8]
 len  = payload_len + 2          (counts type + payload + crc)
 crc8 = DVB-S2, poly 0xD5, over type + payload
 max total 64 bytes
```
Handset→module sync byte: `0xEE` or `0xC8` (both accepted). Module→handset sync: always `0xC8`.

"Extended" frames (types ≥ 0x28) insert `[dest][orig]` as the first two payload bytes.
Addresses: handset `0xEA`, TX module `0xEE`, broadcast `0x00`.

### Frames the handset SENDS
| type | name | payload | notes |
|---|---|---|---|
| `0x16` | RC_CHANNELS_PACKED | 22 B: 16 × 11-bit, LSB-first | 172..1811 (172=988 µs, 992=1500, 1811=2012). Send at **250–500 Hz**, hardware-timed. **CH5 (index 4) is the ARM channel** — module derives `isArmed` from it (>mid = armed) because the frame has no status byte. |
| **`0xAA`** | ELRS_MAVLINK_RAW | 1–60 raw MAVLink bytes | Arbitrary byte-stream chunks; no MAVLink packet alignment required. Only accepted while module Link Mode = MAVLink (default). |
| `0x28` | DEVICE_PING | `[0x00][0xEA]` | Module answers `0x29` DEVICE_INFO. |
| `0x2C` | PARAMETER_READ | `[0xEE][0xEA][param_idx][chunk_idx]` | Module answers `0x2B` (possibly several chunks). |
| `0x2D` | PARAMETER_WRITE | `[0xEE][0xEA][param_idx][value]` | Applies immediately; module re-syncs RX in ~100–300 ms. |

### Frames the handset RECEIVES
| type | name | payload | notes |
|---|---|---|---|
| **`0xAA`** | ELRS_MAVLINK_RAW | ≤60 raw MAVLink bytes | Forward to USB CDC verbatim. One frame per over‑the‑air downlink chunk; on the bench at 250 Hz these were ~31 B each, 18–20/s when saturated. |
| `0x14` | LINK_STATISTICS | `crsfLinkStatistics_t` (10 B): `uplink_RSSI_1`, `uplink_RSSI_2`, `uplink_LQ (0-100)`, `uplink_SNR (i8)`, `active_antenna`, `rf_Mode`, `uplink_TX_Power (0..8 CRSF power enum)`, `downlink_RSSI_1`, `downlink_LQ`, `downlink_SNR` | "uplink" = handset→drone as seen by the RX; "downlink" = drone→handset as seen by the TX. **RSSI bytes are signed dBm on this build** (`tx_main.cpp:147` negates the RX's value; `0xF4` = −12 dBm). Decode robustly: `v = (int8_t)b; dBm = (v <= 0) ? v : -v` (handles the legacy positive‑magnitude convention too). **`rf_Mode` is the rate *enum*** (`expresslrs_RFrates_e`, `common.h:85`), *not* the table index — see the rate table below. ~4–5 frames/s. |
| `0x29` | DEVICE_INFO | `[0xEA][0xEE][name\0][serial u32][hw u32][sw u32][param_count u8][param_ver u8]` | `param_count` bounds the parameter walk. |
| `0x2B` | PARAMETER_SETTINGS_ENTRY | `[0xEA][0xEE][param_idx][chunks_remaining][parent][type][name\0][type-specific…]` | For `type = 0x09 TEXT_SELECTION`: `[options ';'-separated \0][value u8][min u8][max u8][default u8][unit\0]`. Multi-chunk entries: concatenate payloads after the 2-byte chunk header until `chunks_remaining == 0`. See `CRSFEndpoint.cpp::sendParameter`. |
| `0x3A` | HANDSET (timing sync) | ext: subcmd `0x10`, rate/offset be32 | Module asks for a specific RC interval. **Safe to ignore.** Normally 5/s; bursts to 50+/s while the module is nudging timing (seen right after link‑up). Budget for it. |
| `0x02` GPS, `0x07` VARIO, `0x08` BATTERY, `0x0D` BARO_ALT, `0x1E` ATTITUDE, `0x21` FLIGHT_MODE, `0x80` ARDUPILOT_RESP | CRSF telemetry converted from MAVLink by the module (`convert_mavlink_to_crsf_telem`) | All observed on the bench, ~10–20 frames/s combined | Optional for handset UI; MP already gets the raw MAVLink. Must be *tolerated*: the deframer sees ~60–100 non‑`0xAA` frames/s. |

Module→handset sync byte is always `0xC8` (`CRSFRouter::SetHeaderAndCrc`, `CRSFHandset::forwardMessage`).

### Parameter facts for this firmware build
- Parameter indices are 1-based. On the LR1121 build: **1 = "RF Band", 2 = "Packet Rate"**.
  Others of interest by *name*: "Max Power", "Dynamic", "Link Mode". Indices can shift between
  firmware builds → **discover by name at link-up** via the `0x2C` walk; fall back to 2 only if the
  walk fails.
- Packet Rate **write value = `19 − rateIndex`** (`RATE_MAX − 1 − idx`, RATE_MAX = 20). The
  `rf_Mode` you read back in `0x14` is the **enum**, a different number (`common.cpp:61–80` for the
  table, `common.h:85` for the enum):

  | rate | table idx | write value | **`rf_Mode` enum** | OTA pkt | sensitivity | MAVLink down/up | notes |
  |---|---|---|---|---|---|---|---|
  | F1000 8ch (FSK) | 0 | **19** | **11** | 13 B | −101 dBm | ~5 kB/s / ~2.5 kB/s (est.) | default, lowest latency; needs ≥921600 handset baud |
  | 250 Hz LoRa | 1 | 18 | **7** | 8 B | −108 dBm | **620 B/s measured** / ~300 B/s | what you get at 115200/400000 |
  | 200 Hz Full | 2 | **17** | **6** | 13 B | −111 dBm | ~1 kB/s / ~0.5 kB/s (est.) | "long range" mode |
  | 100 Hz Full | 4 | 15 | 3 | 13 B | −112 dBm | ~0.5 / ~0.25 kB/s | only 1 dB better than 200 Full — skip |
  | 50 Hz (4ch) | 6 | 13 | 1 | 8 B | −120 dBm | ~125 / ~60 B/s | heartbeats only; MP unusable |
  Do **not** write values for indices 9–19 (2.4 GHz/dual band; the module's 2.4 GHz port is unconnected).
  Downlink capacity ≈ (packet rate ÷ 2, MAVLink mode forces TLM 1:2) × data bytes per OTA packet;
  the 620 B/s at 250 Hz matched that estimate within a few %, so trust the F1000 estimate to ±20 %.
- Max Power value = level − power_min = **0..3** (10/25/50/100 mW). Dynamic = 0/1.
- Link Mode: leave as MAVLink (the module forces it at every boot anyway).

### Switch mode consequence
The module runs "16ch/2" on the 8-ch rates: each OTA packet carries 8 channels, alternating
ch1–8 / ch9–16. All 16 channels are full 10-bit; sticks refresh at 125–250 Hz (F1000) or
25–50 Hz (200 Hz Full). Arm state is sent in *every* packet as a separate bit.

---

## What's in the STM32 repo today (and what's wrong)

`Kestrel_Gimbal_Test/Core/{Inc,Src}` — untracked `*.h` in git; `freertos.c`/`main.c` modified.
(Re‑checked 2026‑09‑16: unchanged since this table was written — all items below still apply.)

| File | Status | Issue vs. the contract |
|---|---|---|
| `crsf.h` / `crsf.c` | Clean C, host-tested (`tests/test_crsf.c`). CRC, channel pack/unpack, `crsf_build_rc`, `crsf_build_mavlink`, `crsf_parse` all correct. | **`CRSF_FRAMETYPE_MAVLINK_ENVELOPE = 0x3A` is wrong — must be `0xAA`.** `0x3A` is CRSF `HANDSET` and the module routes it as an extended frame (it would read MAVLink bytes as addresses). Comments describe "AirPort"; semantics unchanged. |
| `usart.c` | USART6 PC6/PC7, DMA2 RX circular Stream1 / TX Stream6, IRQ enabled. | **`BaudRate = 460800` — must be `921600`** (not 400000, see *Physical*). Pin choice conflicts with the Kestrel PCB (see Phase F). |
| `link_tx.c/.h` | Solid: TIM2-tick-driven uplink MUX, RC first each tick, MAVLink fills remaining time, bounded TX-done waits, error recovery. 250 Hz RC. | Comments say AirPort; `LINK_TX_DONE_TIMEOUT_MS` comment assumes 460800 (harmless). 250 Hz RC was bench‑proven sufficient with the reference bridge; 500 Hz is optional. |
| `inputs.c` / `sticks.c` | Debounced polling, 2-pos/3-pos helpers, ADC→CRSF mapping. | Channel map is generic (`ch5 = 3-pos switch 1`). **CH5 must be the ARM switch.** Physical pin table (`board.h` PE0..PE13) does not match the PCB (Phase F). |
| `freertos.c` | Tasks exist: `inputTask`, `linkTxTask`, `mavUplinkTask` (USB→`link_tx_queue_mavlink`, done), `mavDownlinkTask` (**empty stub**), `healthTask` (**empty stub**). `usbRxStreamBuffer` fed from `CDC_Receive_FS`. `mavlink_dl_dma_buf[512]` declared, **never used**. | Downlink RX path does not exist. IWDG never refreshed (healthTask stub) — if IWDG is enabled in the `.ioc` the board will reset every ~100 ms. |
| `usbd_cdc_if.c` | RX → stream buffer (good). `CDC_Transmit_FS` is the stock blocking-if-busy call. | No TX queue: back-to-back downlink chunks will be dropped with `USBD_BUSY`. |
| `tests/` | CMake host test for `crsf.c`. | Extend for new frames/parsers. |
| `RETARGET_CHECKLIST.md` | CubeMX recipe (USART6 @460800, PE0..13 inputs, ADC PA0/PA3/PA4/PA6). | Baud and pin assumptions superseded by the PCB (Phase F). |

Nothing here is wasted — every module is kept; the plan is fix, finish, and wire.

---

## Recommended channel map (4 axes + PCB switch nets)

PCB nets (from `transmitter.kicad_pcb`): `roll pitch throt yaw`, `sw_arm`, `sw_emergency_kill`,
`sw_flight_mode1/2` (3-pos), `aux1_sw`, `aux2_sw`, `aux3_sw1/2` (3-pos), `btn_left`, `btn_right`,
`btn1..btn4`.

| CRSF ch | source | ArduPilot side |
|---|---|---|
| 1–4 | roll, pitch, throttle, yaw (AETR) | `RCMAP_*` default |
| **5** | `sw_arm` (2-pos) | `RC5_OPTION = 153` (ArmDisarm). ELRS arm bit derives from this. |
| 6 | `sw_flight_mode1/2` (3-pos → 3 modes; or 6 with a modifier button) | `FLTMODE_CH = 6` |
| 7 | `sw_emergency_kill` | `RC7_OPTION = 31` (Motor Interlock) or 32 |
| 8 | `aux1_sw` → RTL | `RC8_OPTION = 4` |
| 9 | `aux2_sw` | spare |
| 10 | `aux3_sw1/2` (3-pos) | spare |
| 11–16 | `btn_left`, `btn_right`, `btn1..4` (momentary, 2-pos) | camera trigger / relay / spare |

Handset-local (not RC): one button or switch reserved for **rate mode toggle** (Phase D) — pick
one of the momentary buttons (e.g. `btn4`) and keep it off the RC map, or make it a long-press.

---

## Implementation phases

### Phase A — Protocol alignment (small, do first)
Files: `Core/Inc/crsf.h`, `Core/Src/crsf.c`, `Core/Src/usart.c` (and the `.ioc` USART6 baud so
CubeMX regen doesn't revert it), `RETARGET_CHECKLIST.md`, `tests/test_crsf.c`.
1. `CRSF_FRAMETYPE_MAVLINK_ENVELOPE` → rename `CRSF_FRAMETYPE_ELRS_MAVLINK_RAW = 0xAAu`; add
   `CRSF_FRAMETYPE_DEVICE_PING 0x28`, `DEVICE_INFO 0x29`, `PARAM_ENTRY 0x2B`, `PARAM_READ 0x2C`,
   `PARAM_WRITE 0x2D`, `HANDSET 0x3A`; add `CRSF_ADDR_MODULE 0xEE`, `CRSF_ADDR_HANDSET_EA 0xEA`
   (current `CRSF_ADDR_HANDSET` is really the sync byte — keep it as the sync, don't break callers).
2. Add builders: `crsf_build_ext(buf, type, dest, orig, payload, n)` and thin wrappers
   `crsf_build_ping`, `crsf_build_param_read(idx, chunk)`, `crsf_build_param_write(idx, value)`.
3. Baud **921600** in `usart.c` + `.ioc`; fix the comment in `link_tx.c`.
4. Update `test_crsf.c` for the new type byte and the extended builders (CRC over type+dest+orig+payload).
   Golden vector for the RC frame (all 16 channels at 992, sync `0xEE`):
   `EE 18 16 E0 03 1F F8 C0 07 3E F0 81 0F 7C E0 03 1F F8 C0 07 3E F0 81 0F 7C AD` — this exact
   byte string was accepted by the module on the bench.

### Phase B — Downlink receive path (module → handset)
New: `Core/Inc/link_rx.h`, `Core/Src/link_rx.c`. Touch: `freertos.c` (`StartMavDownlinkTask`, RX
event callback), `usart.c`/`.ioc` only if the RX DMA stream needs changes (already circular).
1. Start `HAL_UARTEx_ReceiveToIdle_DMA(BRD_MODULE_UART, mavlink_dl_dma_buf, 512)` once at task start;
   in `HAL_UARTEx_RxEventCallback` push `[old_pos, Size)` (handle wrap) into a stream buffer and
   notify `mavDownlinkTask`. Disable the half-transfer IRQ noise or handle it (`__HAL_DMA_DISABLE_IT(…, DMA_IT_HT)`).
2. Deframer (pure C, host-testable, in `link_rx.c` or a `crsf_deframe.c`): byte-at-a-time state
   machine: hunt for `0xC8` (accept `0xEE` too), read len (2..62), collect, verify CRC, emit frame;
   on CRC fail resync at next candidate sync byte. Reuse `crsf_parse` for validation.
3. Dispatch by type:
   - `0xAA` → `usb_tx_queue(payload, n)` (Phase C).
   - `0x14` → copy into a `LinkStats` struct (mutex/atomic), timestamp; used by health/UI.
   - `0x29`, `0x2B` → hand to the parameter client (Phase D).
   - everything else → count and drop.
4. Stats: frames ok / crc err / resyncs / bytes; expose `link_rx_get_stats()`.
5. Re-arm on `HAL_UART_ErrorCallback` for the RX side too (the existing callback aborts the whole
   UART — split into TX abort vs. RX restart so an RX overrun cannot kill the TX path).

### Phase C — USB CDC transmit queue (handset → tablet)
Files: `USB_DEVICE/App/usbd_cdc_if.c/.h`, `freertos.c`.
1. Add a 2–4 KB TX stream buffer + `usb_tx_queue()`; a small drain that calls `CDC_Transmit_FS`
   with up to 64-byte (or 512 if HS) packets and continues from `CDC_TransmitCplt_FS`
   (`USBD_CDC_ItfTypeDef.TransmitCplt` exists in this middleware version — hook it).
2. If the tablet is detached (VBUS low / not configured), drop with a counter instead of blocking.
3. Never parse MAVLink here; forward bytes as they come (MP handles fragments).

### Phase D — ELRS parameter client + rate switching
New: `Core/Inc/elrs_param.h`, `Core/Src/elrs_param.c`. Touch: `link_tx.c` (a small "control frame"
queue that is sent with RC priority but ahead of MAVLink chunks — or simply push control frames
through the existing MAVLink stream with a separate higher-priority queue drained first),
`freertos.c` (call from `healthTask` or a new low-rate task), `inputs.c` (the toggle input).
1. On link-up (first `0x14` received, or timer): send `0x28` ping; parse `0x29` → `param_count`.
2. Walk `0x2C` for idx 1..param_count (one outstanding request at a time, 100 ms timeout, retry 3×),
   reassemble chunks, parse names; record indices of `"Packet Rate"`, `"Max Power"`, `"Dynamic"`.
   Fallback: Packet Rate = 2. Cache results; re-walk only after the module reconnects.
3. `elrs_set_rate(RATE_F1000 | RATE_200FULL)` → `0x2D` write with value 19 / 17. Confirm via
   `rf_Mode` in the next `0x14` frames — **enum values 11 / 6** (not 0 / 2); retry once if
   unconfirmed after 500 ms. If `rf_Mode` reads 7 (250 Hz) after asking for F1000, the handset
   baud is clamping the rate (see *Physical*), not a parameter bug.
4. Trigger: a dedicated handset control (long-press ≥1 s to avoid accidental flaps), with ≥5 s
   lockout between switches. Optionally auto-degrade when `uplink_LQ < 50` for >2 s and
   auto-recover when `> 90` for >10 s — manual first, automatic only after bench validation.
5. Expose `elrs_param_get_state()` for LEDs/buzzer.

### Phase E — Health, failsafe, indicators
Files: `freertos.c` (`StartHealthTask`), `inputs.c` (map), `board.h`.
1. `healthTask`: refresh IWDG **only** when `linkTxTask` has sent an RC frame in the last 100 ms and
   `inputTask` has run (use "last activity" timestamps); blink `LED_STATUS`.
2. `LED_LINK` = `0x14` received within 500 ms; `LED_WARN`/buzzer = LQ < 30 or module silent > 1 s
   or VBAT low.
3. Apply the channel map above in `inputs_build_channels()`; ensure CH5 = `sw_arm` and that the
   arm switch reads *disarmed* (172) on any input fault.
4. On tablet detach, keep RC running; on module silence, keep sending RC (the module's watchdog
   needs frames to re-sync). Do not reboot or stop the UART.

### Phase F — Kestrel PCB pin reconciliation (blocks running on the real board)
The checklist/`board.h` picked pins that are free on the Nucleo; the Kestrel PCB routes differently.
Pad → net list extracted from `transmitter.kicad_pcb` (U1 = STM32F767VITx LQFP100):
```
15 roll    18 pitch   25 throt   29 yaw            (analog)
17 aux1_sw 35 aux2_sw 36 sw_flight_mode1 46 sw_flight_mode2 47 aux3_sw1 51 aux3_sw2
63 sw_arm  65 sw_emergency_kill 66 btn_left 78 btn_right 90 btn1 91 btn2 95 btn3 96 btn4
55 UART3_RX 56 UART3_TX   (module connector nets)
70 USB_DM 71 USB_DP  72 SWDIO 76 SWCLK  81 Status_LED 82 Buzzer  14 NRST 94 BOOT0
```
1. Map each pad to its port/pin from the STM32F767 LQFP100 pin table (datasheet) — **verify, do not
   assume**; in particular confirm which USART owns pads 55/56 (nets are named `UART3_*`; if they
   land on `PD8/PD9` that is USART3, and USART6 on PC6/PC7 is impossible because pad 63 = `sw_arm`).
2. Build `BOARD_KESTREL` tables in `board.h`: `BRD_MODULE_UART`, ADC channel/rank order
   (roll/pitch/throt/yaw), `BRD_DIGITAL_INPUTS(X)` with the real ports/pins, LED/buzzer pins.
3. Regenerate the `.ioc` for the Kestrel variant (or a second `.ioc`): USART + DMA streams, ADC1
   ranks, GPIO inputs with pull-ups, `HSE_ON` (crystal, not bypass).
4. Keep `BOARD_NUCLEO` working for bench tests with the module on a Nucleo.

---

## Bench reference: the PC handset emulator (read this before writing `link_rx.c`)

`F:\ExpressLRS\src\python\test_tools\kestrel_handset_bridge.py` is a ~250‑line Python program that
does exactly what the STM32 must do, and it was proven end to end on 2026‑09‑16 against the real
modules and a Pixhawk 6C Mini. **Treat it as the executable spec.** Read it once, then mirror its
behaviour:

- `serial_writer`: a fixed‑period tick (250 Hz). Every tick sends one RC frame (sync `0xEE`);
  if tunnel bytes are pending it appends **at most one** `0xAA` frame (≤60 B) in the same write,
  rate‑limited by a token bucket (3000 B/s default) so the module's 1024‑byte uplink buffer
  cannot overflow (`HandsetMavlinkIn` drops silently when it is full).
- `CrsfParser`: byte‑stream deframer — hunt sync (`0xC8`/`0xEA`/`0xEE`), length sanity (4..64
  total), wait for the full frame, CRC over `type+payload`, resync by dropping one byte on failure.
  It ran for minutes at ~100 frames/s with 2–3 CRC errors total on a USB adapter.
- Dispatch: `0xAA` → GCS verbatim; `0x14` → link‑stats struct; everything else counted and dropped.
- Nothing parses MAVLink anywhere. The GCS copes with fragments.

Run it yourself to sanity‑check the module before blaming the STM32 (PlatformIO's venv has pyserial):
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" F:\ExpressLRS\src\python\test_tools\kestrel_handset_bridge.py --port COM3 --baud 115200
```
(115200 because the bench USB‑serial adapter cannot do 400 k/921.6 k; the STM32 will use 921600.)
Its once‑per‑second stats line is the format to copy for a debug console on the handset:
```
rc 250/s  mav up 21 B/s  down 620 B/s  crc_err 3  dropped 0  rx[0x14:4 0x21:1 0x3A:6 0x80:1 0xAA:20] | UL rssi -14dBm lq 100% snr 10 | DL rssi -14dBm lq 100% snr 9 | mode 7 pwr 3
```

Facts established on the bench that the STM32 design must respect:
- **RC frames from boot, forever.** No RC → module silent → no RF at all. Not "degraded": *off*.
- **Link‑up sequence** after the first valid frame: `0x3A` + `0x14` start immediately (module
  considers the handset connected); TX radiates ~1.5 s later; RX locks a few seconds after that;
  only then does `DL lq` become non‑zero and `0xAA` traffic start. Health logic should not treat
  "no `0xAA` for 5 s after boot" as a fault.
- Optional: sending CRSF `COMMAND` (`0x32`, ext, payload `[0x10][0x05][model_id]`) right after
  the first RC frame skips the 1.5 s `awaitingModelId` delay (`TXModuleEndpoint.cpp:60`). Not required.
- **Downlink saturates cleanly.** At 250 Hz the tunnel sat at 620 B/s for the whole param fetch;
  ArduPilot's default `SR2_*` stream rates exceed that, so the RX drops whole MAVLink messages
  (harmless, MAVLink is lossy). Consider lowering `SR2_EXTRA1/2/3`, `SR2_POSITION`, `SR2_RAW_SENS`
  to 1–2 Hz on the aircraft, at least for the long‑range rate.
- Handset‑side UART load at F1000 is small: ~1 kB/s of CRSF telemetry/sync + the tunnel (≤5 kB/s).
  The module writes up to 128 B per received RC frame; at 921600 that is 1.4 ms of the 4 ms period.
- The `0xAA` chunking is the module's, not yours: expect frames of arbitrary size ≤60 with
  MAVLink packets split across them. Forward bytes, never re‑frame.

Aircraft‑side settings that worked (ArduPilot, Pixhawk 6C Mini, RX on TELEM2):
`SERIAL2_PROTOCOL = 2`, `SERIAL2_BAUD = 460`, `BRD_SER2_RTSCTS = 0`, `SYSID_MYGCS = 255` (the RX
injects `RC_CHANNELS_OVERRIDE` as sysid 255, so MYGCS must stay 255 or the override is ignored).

---

## Verification

Host (no hardware): `cd STM32/tests && cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure`
— extend with: `0xAA` build/parse round-trip, extended-frame CRC, deframer resync on garbage/CRC errors,
parameter entry chunk reassembly (feed a captured `0x2B` sequence), link-stats decode (signed RSSI).

Bench, incremental (each step was passed with the Python bridge on 2026‑09‑16, so a failure here is
on the STM32 side):
0. **Known‑good baseline**: run the Python bridge against the module first. If *it* fails, fix the
   module/RX/aircraft before touching the STM32.
1. **UART alone**: logic analyser on the module UART at 921600; RC frames every 4 ms (26 B, first byte
   `0xEE`/`0xC8`, last = CRC). Within 1 s the module answers with `0x3A` (every 200 ms) and `0x14`
   (every ~250 ms) — **if it sends nothing, the baud is wrong**. TX LED goes solid ~1.5 s after the
   first frame (note: the "waiting for handset" and "radio failed" blink patterns are identical on
   this board, so a blinking LED is not diagnostic). `DL lq` in `0x14` reaches 100 % once the RX is
   powered and locked; if the RX was running an old AirPort build it may need a power cycle (that
   build auto‑switched to WiFi after 60 s without a link).
2. **RC end-to-end**: RX module UART → FC (or a USB-serial + MAVLink inspector): `RC_CHANNELS_OVERRIDE`
   at 100 Hz tracks the sticks; CH5 toggles with `sw_arm`. With the Pixhawk on USB in parallel,
   Mission Planner's Radio Calibration page shows the channels live.
3. **MAVLink downlink**: FC connected to RX at 460800 → heartbeats appear in Mission Planner on the
   tablet within ~2 s of RX lock; `RADIO_STATUS` present; `0x21`/`0x1E`/`0x08` CRSF telemetry also
   arriving proves the module is parsing the stream. Verify no `USBD_BUSY` drops (counter = 0 at 5 kB/s).
4. **MAVLink uplink**: MP "Fetch Params" completes (~8–10 s at F1000; it took ~60 s at the bench's
   250 Hz, saturated at 620 B/s); mission upload of 50 WPs completes; arm/disarm from MP works.
5. **Rate switch**: trigger the handset control → within 500 ms `rf_Mode` in `0x14` changes 0 ↔ 2,
   MP keeps its link (throughput drops to ~1 kB/s in 200 Hz Full); RC never drops (watch
   `RC_CHANNELS_OVERRIDE` continuity on the FC side).
6. **Robustness**: unplug tablet mid-stream (RC continues); power-cycle the module (handset reconnects,
   param indices re-discovered, LED_LINK recovers); hold the STM32 in the debugger for 200 ms
   (IWDG behaves as intended: reset only if tasks are truly wedged).

---

## Additional context available to the agent

**ExpressLRS firmware (the other end of the wire)** — full working tree at `F:\ExpressLRS`
(branch `master` + uncommitted Kestrel changes; a fork/branch may exist by the time you read this —
check `git -C F:\ExpressLRS status`). Read-only for this task. Key entry points are listed at the top
of this document. If a protocol detail here and the code disagree, **the code wins** — quote the file
and line when you report the discrepancy.

**Handset PCB (KiCad 8)** — `C:\Users\erich\OneDrive - University of Ottawa\Year 4\Capstone\
Samuel Marchetti's files - CEG4912\05. Hardware\Transmitter\transmitter\transmitter.kicad_pcb`
(and `.kicad_sch`). The ELRS module PCB is next to it in `..\elrsmodule\`. Both are s-expression text.
To regenerate the pad→net table for U1 (STM32F767VITx) yourself:

```bash
# in the transmitter/ folder; U1 footprint starts at the line printed by the first command
grep -n '(property "Reference" "U1"' transmitter.kicad_pcb
awk 'NR>=START && NR<=START+2500 {
  if ($0 ~ /\(pad "/)  { match($0,/\(pad "[^"]+"/); p=substr($0,RSTART+6,RLENGTH-7) }
  if ($0 ~ /\(net "/)  { match($0,/\(net "[^"]+"/); n=substr($0,RSTART+6,RLENGTH-7);
                         if (n!="GND" && n !~ /unconnected/) print "pad " p " -> " n } }' transmitter.kicad_pcb
```
Replace `START` with the footprint's line number. Then map pad numbers to ports with the
STM32F767xx datasheet LQFP100 pinout table (DS11532, "Pin definitions") — that table is the
authority for Phase F, not this document.

---

## Notes for the user (not the STM32 agent)

- The ELRS changes live in `F:\ExpressLRS` (uncommitted): `src/include/crsf_protocol.h`,
  `src/lib/Handset/handset.h`, `src/lib/Handset/CRSFHandset.cpp`, `src/lib/Handset/devHandset.cpp`,
  `src/src/tx_main.cpp`, `src/user_defines.txt`, `src/python/test_tools/kestrel_handset_bridge.py`,
  plus the untracked `src/hardware/{TX,RX}/KESTREL LR1121.json` and the `kestrel` entry in
  `src/hardware/targets.json` (`.gitignore` was edited to stop ignoring `src/hardware`). Copy them
  into `kestrel/eLRS Module/ExpressLRS/src` (your vendored copy) so the repo is self-contained —
  as of 2026‑09‑16 the vendored copy only holds `user_defines.txt`, and it is the *AirPort* variant.
- Flash (from `F:\ExpressLRS\src`, with `~\.platformio\penv\Scripts` on PATH):
  `$env:ELRS_UNIFIED_CONFIG="kestrel.tx_900.kestrel"; pio run -e Unified_ESP32_LR1121_TX_via_UART -t upload --upload-port COMx`
  and `kestrel.rx_900.kestrel` / `…_RX_via_UART` for the receiver. **Flash both** whenever the
  binding phrase or the AirPort define changes — both are baked into the binary, not settings.
- **Tablet directly on the module does not work with this build.** The module needs a CRSF handset
  (RC frames + `0xAA` tunnel); raw MAVLink on its UART is ignored and it never radiates. For a
  handset‑less demo use the AirPort build (`-DUSE_AIRPORT_AT_BAUD=…`, both modules, MAVLink‑only,
  no RC) — that is what worked before this design. The STM32 handset is the product path.
- The bench USB‑serial adapter cannot generate 400000 or 921600 (silence from the module = wrong
  baud). For future module‑only tests without the STM32, use `--baud 115200` and accept 250 Hz, or
  get an FTDI‑based adapter (FT232 does 921600 exactly).
