# CubeMX retarget recipe — NUCLEO-F767ZI (LQFP144) → custom STM32F767VITx (LQFP100)

Implements **Part 1** of the re-plan. Target device is the custom **STM32F767VITx**; every pin is
chosen so the same source also builds for a NUCLEO-F767ZI (**Part 0**). CubeMX rebuilds and
validates its own model when the `.ioc` opens, so the reliable flow is: identity edit (done) →
GUI steps below → generate.

Base tooling in the `.ioc`: `MxCube 6.18`, `FW_F7 V1.17.4`. **Tag first:** `git tag pre-f767vi-retarget`.

---

## Step 0 — Device identity (ALREADY DONE in `Kestrel_Gimbal_Test.ioc`)

These keys were text-edited with CubeMX closed:

| Key | New value |
| :-- | :-- |
| `Mcu.CPN` | `STM32F767VIT6` |
| `Mcu.Name` / `Mcu.UserName` | `STM32F767VITx` |
| `Mcu.Package` | `LQFP100` |
| `PCC.MCU` / `PCC.PartNumber` | `STM32F767VITx` |
| `ProjectManager.DeviceId` | `STM32F767VITx` |
| `board=NUCLEO-F767ZI`, `boardIOC=true` | *deleted* |

Open the `.ioc` in CubeMX / STM32CubeIDE and **accept the migration prompt**. It reloads on the
100-pin part and silently drops every port **F/G** pin — this removes ETH `PG11/PG13`,
`USB_PowerSwitchOn` `PG6`, `USB_OverCurrent` `PG7`. ETH will show errors (fixed in Step 3).

---

## Step 1 — Clocks (RCC) — mostly keep

The current tree is already 216 MHz from an 8 MHz HSE. In **Clock Configuration**, confirm after
reload:

- `HCLK = 216 MHz`, `APB1 = 54 MHz` (`×2 = 108 MHz` timers), `APB2 = 108 MHz` (`×2 = 216 MHz` timers)
- `PLLQ` output `= 48 MHz` → USB
- `USART6` clock `= 108 MHz`, `To ADC = 27 MHz` (PCLK2/4)

**RCC pane:**
- **HSE = BYPASS Clock Source** (keep). Matches the Nucleo's 8 MHz ST-LINK MCO.
  *Custom-board delta (§0.2): change to "Crystal/Ceramic Resonator" — or hand-patch
  `RCC_OscInitStruct.HSEState = RCC_HSE_ON` — once the real PCB with its crystal is in hand.*

**CORTEX_M7 pane:**
- **CPU ICache = Enabled**
- **CPU DCache = Disabled** (enabling needs the MPU non-cacheable DMA region, plan §2.6 — defer)

**SYS pane:** Timebase Source = **TIM6** (keep), Debug = **Serial Wire** (keep), `PB3` = SWO (keep).

---

## Step 2 — Remove peripherals

- **ETH** → Mode = **Disable**. Confirm `PA1 PA2 PA7 PC1 PC4 PC5 PB13` return to unused — leave
  them unused (Part 0: the Nucleo PHY loads these).
- **USART3** → Mode = **Disable**. Releases `PD8/PD9` — leave them free so the Nucleo ST-LINK VCP
  keeps working for debug. Its `DMA1_Stream1/3` and NVIC lines clear with it.
- **GPIO** → set each of these to **Reset_State**: `PB0 PB2 PB4 PB5 PB7 PB8 PB9 PB10 PB11 PB12 PB14`,
  `PC2 PC6 PC8 PC9 PC10`, `PC13` (scattered Nucleo LEDs / user button / old input bank).
- Old ADC pins to release: `PC0 PC3 PA5` (re-picked in Step 4).
- `PA8` (`USB_SOF`) and `PA10` (`USB_ID`) → **Reset_State** (not needed for self-powered device-only
  CDC; optional but frees the pins).

---

## Step 3 — Module UART: USART6  (§0.1, §1.3)

Connectivity → **USART6**:
- Mode = **Asynchronous**; Hardware Flow Control = None.
- Parameters: **921600** Bd (was 460800 — the ELRS module cannot autobaud 460800, and 400000 clamps it to 250 Hz; see `ELRS_HANDOFF.md` §Physical), Word 8, Parity None, Stop 1, Direction RX+TX, Over Sampling 16.
- Pins: **`PC6` = USART6_TX**, **`PC7` = USART6_RX** (reassign if CubeMX picks others).
  GPIO speed both → **Very High**.
- **DMA Settings** → Add:
  | Request | Stream (let CubeMX assign) | Mode | Width | Priority | FIFO |
  | :-- | :-- | :-- | :-- | :-- | :-- |
  | `USART6_RX` | a `DMA2` stream (e.g. Stream1) | **Circular** | Byte/Byte | Very High | Disable |
  | `USART6_TX` | a `DMA2` stream (e.g. Stream6) | **Normal** | Byte/Byte | Very High | Disable |
  Verify neither lands on `DMA2_Stream0` (ADC1).
- **NVIC Settings** (USART6 pane): tick **USART6 global interrupt**.

---

## Step 4 — ADC1: 5 channels, timer-triggered  (§1.4, §0.5)

Analog → **ADC1**:
- Enable **IN0, IN3, IN4, IN6, IN9** → pins `PA0 PA3 PA4 PA6 PB1` (`PB1` = VBAT sense via the
  board divider).
- Parameter Settings:
  | Parameter | Value |
  | :-- | :-- |
  | Scan Conversion Mode | **Enabled** |
  | Continuous Conversion Mode | **Disabled** |
  | DMA Continuous Requests | **Enabled** |
  | External Trigger Conversion Source | **Timer 2 Trigger Out event** |
  | External Trigger Conversion Edge | **Rising edge** |
  | Number Of Conversion | **5** |
  | Rank 1..4 | `IN0, IN3, IN4, IN6` — Sampling **144 Cycles** |
  | Rank 5 | `IN9` (VBAT) — Sampling **480 Cycles** |
  → `adc_raw[0..3]` = gimbal axes, `adc_raw[4]` = VBAT (plan §2.2).
- **DMA Settings**: `ADC1` on `DMA2_Stream0`, **Circular**, Half Word / Half Word, Priority High,
  FIFO Disable, Increment Memory only.
- **NVIC**: `DMA2 Stream0 global interrupt` stays enabled.

---

## Step 5 — TIM2: sample + frame cadence  (§1.5)

Timers → **TIM2**:
- Clock Source = **Internal Clock**. No channels.
- **First confirm TIM2's input clock** in Clock Configuration (APB1 timer domain, expected
  **108 MHz** — the stale `RCC.TIM2Freq_Value` in the old `.ioc` is not authoritative).
- Parameters for **250 Hz** from 108 MHz:
  - Prescaler = `107`  (÷108)
  - Counter Period = `3999`  (÷4000)  → 108e6 / 108 / 4000 = **250 Hz** (`RC_RATE_HZ`)
  - Counter Mode = Up; auto-reload preload = Enable
- **Trigger Output (TRGO) Parameters** → Trigger Event Selection = **Update Event**.
- **NVIC**: tick **TIM2 global interrupt**.

---

## Step 6 — IWDG  (§1.8)

System Core → **IWDG** → Activated:
- Prescaler = **64**, Reload = **50** → ≈100 ms at LSI ≈32 kHz. LSI tolerance is wide
  (~60–150 ms actual) — fine for a hung-task reset. Refreshed by `healthTask` only.

---

## Step 7 — USB  (§1.6)

- Connectivity → USB_OTG_FS = **Device_Only** (keep). Middleware → USB_DEVICE = **CDC** (keep).
- **Keep VBUS sensing**: `PA9 = USB_OTG_FS_VBUS` stays — the handset is battery-powered but must
  detect tablet attach/detach.
- NVIC priority set in Step 9.

---

## Step 8 — GPIO: LEDs + digital inputs

**Status LEDs** (Nucleo dev — become the `board.h` LED macros, §0.2): re-add as **GPIO_Output**,
push-pull, no pull, Low speed, initial level Low:
- `PB0` → label `LED_STATUS`
- `PB7` → label `LED_LINK`
- `PB14` → label `LED_WARN`

**Digital inputs — 14 on port E** (§1.7): set `PE0` … `PE13` each = **GPIO_Input**, **Pull-up**.
- 6 momentary buttons + 4× 2-position switches + 2× 3-position switches (2 pins each) = 14.
- `PE14/PE15` left spare. Label per the §2.3 channel map when assigned. Polled, no EXTI.

---

## Step 9 — NVIC priorities  (§2.7)

NVIC tab, Priority Group = **4** (keep). All FromISR-calling handlers must be numeric priority ≥ 5.

| Interrupt | Preempt priority |
| :-- | :-- |
| TIM2 global interrupt | **5** |
| USART6 global interrupt | **5** |
| DMA2 stream — USART6_RX | **5** |
| DMA2 stream — USART6_TX | **5** |
| DMA2 Stream0 (ADC1) | **6** |
| USB OTG FS global interrupt | **6**  *(was 5)* |
| TIM6-DAC global interrupt (timebase) | 15 *(keep)* |

---

## Step 10 — FreeRTOS (CMSIS-RTOS v2)  (§1.9, §2.7)

Middleware → **FREERTOS**:
- **Config parameters:** `TOTAL_HEAP_SIZE` → **49152**; `CHECK_FOR_STACK_OVERFLOW` → **Option 2**;
  `USE_MALLOC_FAILED_HOOK` → **Enabled**. Keep `USE_NEWLIB_REENTRANT = Enabled`.
- **Tasks and Queues → Tasks:** replace the current 3 with (stack in **words**):

  | Task | Entry function | Priority | Stack |
  | :-- | :-- | :-- | :-- |
  | `linkTxTask` | `StartLinkTxTask` | `osPriorityRealtime` | 512 |
  | `inputTask` | `StartInputTask` | `osPriorityHigh` | 512 |
  | `mavUplinkTask` | `StartMavUplinkTask` | `osPriorityNormal` | 512 |
  | `mavDownlinkTask` | `StartMavDownlinkTask` | `osPriorityNormal` | 512 |
  | `healthTask` | `StartHealthTask` | `osPriorityLow` | 384 |

- **Do not** add queues / semaphores / stream buffers in the CubeMX tabs — they are created by hand
  in `freertos.c` USER CODE so all wiring lives in one place (plan §2.4–2.7).

---

## Step 11 — Project Manager

- Project name: keep `Kestrel_Gimbal_Test` (rename to `Kestrel_Handset` is a separate chore — it
  drags the `.launch` and the linker-script filenames).
- Code Generator: **Generate peripheral initialization as a pair of '.c/.h' files per peripheral**
  = checked; **Keep User Code when re-generating** = checked.
- Toolchain = STM32CubeIDE.

---

## Step 12 — Generate + first build

1. `GENERATE CODE`.
2. Expect changed/new: `main.{c,h}`, `gpio.c`, `dma.c`, `adc.c`, **`usart.c`** (USART6 now),
   **`tim.c`**, **`iwdg.c`**, `stm32f7xx_it.c`, `stm32f7xx_hal_msp.c`, `FreeRTOSConfig.h`,
   `freertos.c` (new task stubs). **Deleted:** `eth.c/.h`.
3. Linker scripts `STM32F767ZITX_*.ld`: memory map is identical on the VI (512 KB RAM / 2 MB
   flash) so they still link. Rename to `..._VITX_...` only if you also update the two `-T` refs in
   `.cproject` and the `.launch`.
4. Build. Link will fail until the Part 2 modules exist — expected. The peripheral layer and every
   `MX_*_Init()` must compile clean.

---

## Cross-check vs. the plan

| Item | End state | Plan |
| :-- | :-- | :-- |
| Module UART | USART6 `PC6/PC7` (Nucleo) — Kestrel PCB uses USART3, see handoff Phase F — **921600**, DMA2 ×2, global IRQ | §0.1, §1.3 |
| ADC1 | 5 ch `PA0 PA3 PA4 PA6 PB1`, TIM2_TRGO, circular DMA | §1.4, §0.5 |
| Cadence | TIM2 250 Hz, TRGO = Update, IRQ pri 5 | §1.5 |
| Watchdog | IWDG ≈100 ms | §1.8 |
| USB | Device / CDC, VBUS sense on `PA9` | §1.6 |
| Digital in | 14 × `PE0..PE13`, pull-up, polled | §1.7 |
| Removed | ETH, USART3, all port F/G | §1.2, §0 |
| NVIC | app IRQs pri 5–6, group 4 | §2.7 |
| Clock | 216 MHz, HSE 8 MHz BYPASS (Nucleo) | §1.1, §0.2 |
