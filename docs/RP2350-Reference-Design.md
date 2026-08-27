# Wingflight RP2350B Reference Design (WF-RP2350B-REF01)

Status date: 2026-08-27
Resolves: [RP2350-Porting-Plan.md](RP2350-Porting-Plan.md) Phase 0 — "Specific
board / reference design to bring up first."

This is a **written hardware specification**, not a schematic. It gives a
hardware designer everything needed to lay out a board and everything the
firmware side needs to ship matching default resource assignments: chip
variant, external components, and a full GPIO pin table that has been
checked pin-by-pin against the actual driver source in
[`src/main/drivers/bus_spi_pico.c`](../src/main/drivers/bus_spi_pico.c),
[`serial_uart_pico.c`](../src/main/drivers/serial_uart_pico.c), and
[`adc_pico.c`](../src/main/drivers/adc_pico.c) on `RP2350_support` (not
derived from general RP2040 knowledge, since RP2350B's function-select
tables differ from plain RP2040 once GPIO32-47 are in play). It has **not**
been built or electrically verified — treat every pin assignment as
"compiles against the real candidate list," not "bring-up tested."

## 1. Scope and why this variant

- **Chip: RP2350B** (QFN-80, 48 GPIO, external QSPI boot flash), not
  RP2350A/RP2354A/RP2354B. Wingflight is a fixed-wing FC firmware — the
  target pin/servo/UART count in this design (8 servos, 4 motor outputs, 2
  hardware UARTs, 2 PIO soft-serial, I2C baro, dedicated blackbox flash +
  SD) needs the B-package's 48 GPIO; RP2350A's 30 GPIO does not leave
  enough headroom without cutting features.
- External QSPI flash (not RP2354B's on-die 2MB) so blackbox firmware
  updates aren't flash-size-constrained the way RP2354B's 2MB die would be
  once firmware + config + custom-defaults are accounted for — see the
  porting plan's blackbox storage decision (2026-08-26).
- This design targets `TARGET=RP2350B` in `RP2350_UNIFIED` — it is a
  **unified target board**: every sensor/bus is runtime-detected via CLI
  `resource`/`gyro_1_bustype`/`baro_hardware` config, not compiled in as a
  fixed board file. The pin table below is this design's *recommended
  default wiring*, meant to become a `.config` custom-defaults file (see
  §8), not a hard requirement enforced by firmware.

## 2. Firmware feature scope at time of writing — design around this

Read before finalizing headers/connectors — some things a "normal" FC board
would break out are **not yet functional** on this port, per
[`target.h`](../src/main/target/RP2350_UNIFIED/target.h) and the porting
plan:

- **CRSF, GHST, FPort, and every other serial RX protocol except SBUS are
  `#undef`'d.** Only `USE_SERIALRX_SBUS` is enabled. Wire the RX UART for
  general duplex use (TX+RX both broken out) so CRSF works the moment it's
  ported, but don't advertise CRSF support on silkscreen/BOM yet.
- **GPS rescue and GPS nav are `#undef`'d.** The GPS UART works as a plain
  serial port (for a GPS module's NMEA/UBX stream) but the firmware
  doesn't yet act on it beyond that.
- **No OSD.** Don't route an OSD/VTX connector.
- **`SERIAL_BIDIR` (true single-wire half-duplex, e.g. SmartPort/S.Port,
  SmartAudio) is explicitly unsupported** — `openSoftSerial()` refuses it.
  If you want S.Port telemetry later, wire it as two separate pads (TX/RX)
  with an external diode-mix or a resistor half-duplex adapter on the
  connector, not as a single MCU pin doing both directions.
- **SBUS inversion needs no external inverter hardware** — RP2350's
  pad-level input/output override inverters handle `SERIAL_INVERTED` in
  firmware. Do not add an inverting transistor stage like some STM32
  boards use; just wire the receiver's SBUS line straight to the RX UART's
  RX pad.
- RAM headroom was 79.4% (416KB/512KB) as of the last porting-plan entry —
  irrelevant to the PCB, but a sign this firmware is still mid-port; expect
  default-config firmware size/RAM to move before this board is finalized.

## 3. Block diagram

```
                +--------------------------------------------------+
                |                   RP2350B (QFN-80)                |
 3V3 -----------|VDDx, IOVDD, ADC_AVDD                              |
                |                                                    |
 W25Q64 (8MB) --|QSPI_SCLK/SS/SD0-3 (dedicated pins, not GPIO)       |
 12MHz XTAL ----|XIN/XOUT (dedicated)                                |
 USB-C ---------|USB_DP/DM (dedicated)                               |
 SWD header ----|SWCLK/SWDIO (dedicated)                             |
 RUN button ----|RUN (dedicated, active-low reset)                   |
                |                                                    |
 BMI270 --------|SPI0: GPIO0/1/2/3/5     (IMU: MISO/CS/SCK/MOSI/INT) |
 Baro (I2C) ----|I2C1: GPIO18/19          (SDA/SCL)                  |
 Blackbox flash-|SPI1: GPIO8/9/10/11      (MISO/CS/SCK/MOSI)         |
 microSD -------|SPI1 shared: GPIO13/15   (CS/CD, bus = GPIO8/10/11) |
 RX (SBUS) -----|UART0 hw: GPIO16/17      (TX/RX)                    |
 GPS/Telem -----|UART1 hw: GPIO20/21      (TX/RX)                    |
 Spare serial --|PIO soft-serial x2: GPIO6/7, GPIO22/23               |
 ESC x4 (DSHOT)-|PIO0: GPIO32/33/34/35                                |
 Servo x8 ------|PWM: GPIO4/12/14/24/25/29/39/43                     |
 LED strip -----|PIO2: GPIO26                                        |
 Buzzer --------|PWM: GPIO37                                         |
 VBAT/CURR/VBEC-|ADC: GPIO40/41/42 (RP2350B ADC range is GPIO40-47)  |
 Spare GPIO ----|GPIO27,28,30,31,36,38,44,45,46,47 (10 pins, header) |
                +--------------------------------------------------+
```

## 4. Power

- Input: 2S-6S LiPo direct to a buck/BEC stage (this is a fixed-wing FC;
  match your target ESC's BEC assumptions, or add an onboard buck if the
  board is meant to run standalone off battery without a separate BEC).
- Onboard 5V→3.3V regulation for the MCU/sensors rail. RP2350 also has an
  internal switched-mode core regulator (VREG) that needs its own
  inductor/cap per the RP2350 datasheet's reference regulator circuit —
  **cross-check the exact VREG pinout/component values against the RP2350
  datasheet** before finalizing; this doc does not re-derive that circuit.
- Decoupling: one 100nF ceramic per VDD/IOVDD pin pair minimum, plus a bulk
  cap (1-10µF) near the QSPI flash and near the regulator output — standard
  RP2350 practice, follow the datasheet's decoupling table for exact pin
  list (QFN-80 has more power pins than RP2040's QFN-56; don't assume the
  RP2040 Pico's decoupling layout carries over 1:1).
- **BOOTSEL differs from RP2040.** RP2040's BOOTSEL is a fixed button that
  grounds a QSPI pin at reset. RP2350 uses an OTP-configurable GPIO bootsel
  mechanism instead. **Confirm the exact RP2350B bootsel wiring against the
  datasheet before copying any RP2040 Pico boot-button reference circuit —
  this is a real bring-up risk if wired like RP2040.**

## 5. MCU + boot flash

- MCU: RP2350B, QFN-80.
- Boot/XIP flash: external QSPI NOR, **W25Q64JV (8MB)**, matching
  `RP2350_UNIFIED/target.mk`'s `PICO_FLASH_SIZE_BYTES=8388608` and
  `PICO_BOOT_STAGE2_CHOOSE_W25Q080=1` default. Wired to the RP2350B's
  dedicated QSPI pins (not general-purpose GPIO — those pins don't appear
  in the GPIO0-47 table above).
- Crystal: 12MHz on XIN/XOUT with load caps per datasheet (RP2350 requires
  an external crystal; there is no internal RC option suitable for
  USB-timing-accurate operation).
- SWD debug header (SWCLK/SWDIO/3V3/GND), standard 3-pin or Tag-Connect
  footprint — useful for early bring-up before USB/UF2 flashing is trusted.

## 6. IMU — SPI0

BMI270 is the reference IMU: it's the one accgyro driver explicitly vendored
into the build (`lib/main/BoschSensortec/BMI270-Sensor-API/bmi270_maximum_fifo.c`,
pulled in by `RP2350_UNIFIED/target.mk`'s `TARGET_SRC`), so it's the
best-exercised path even though the firmware compiles in every SPI
accgyro driver (ICM42688P, ICM20689, BMI323, BMI088, MPU6000/6500, ...) and
auto-detects at runtime.

| Signal | GPIO | Note |
|---|---|---|
| SCK  | 2 | SPI0, from `bus_spi_pico.c`'s SPI0 SCK candidate list `{2,6,18,22,34,38}` |
| MISO | 0 | SPI0 MISO candidate list `{0,4,16,20,32,36}` |
| MOSI | 3 | SPI0 MOSI candidate list `{3,7,19,23,35,39}` |
| CS   | 1 | plain GPIO, no fixed-function constraint |
| INT1 (EXTI) | 5 | plain GPIO |

Mount the IMU on its own small island with a ground moat if practical, and
keep SPI0 traces short — standard gyro layout practice, no PICO-specific
change from STM32 boards here.

## 7. Barometer — I2C1 (optional, auto-detected)

| Signal | GPIO |
|---|---|
| SDA | 18 |
| SCL | 19 |

Any of `USE_BARO_BMP280/BMP388/BMP581/DPS310/MS5611/QMP6988/LPS` works via
runtime detection; recommend **DPS310** or **BMP280** as the reference part
(cheap, common, good driver coverage). GPIO18/19 land on I2C1 per RP2350's
`floor(pin/2) mod 2` I2C-bus-select rule (SDA even, SCL odd) — **cross-check
this pair against the RP2350B datasheet's GPIO function table**;
`bus_i2c_pico.c` takes any CLI-configured pin without a fixed candidate
array (unlike SPI/UART), so this pair isn't driver-verified the way the
SPI/UART pins below are.

## 8. Blackbox storage — dedicated SPI flash + microSD (SPI1, shared bus)

Per the porting plan's 2026-08-26 decision: blackbox logging uses a
**separate physical flash chip from the boot/firmware flash**, plus a
microSD slot — never the QSPI chip in §5, to avoid multicore XIP-write
contention and RP2354B's tight on-die flash margin (not applicable here
since this design uses RP2350B, but the separation is kept for consistency
across all four target variants).

| Signal | GPIO | Note |
|---|---|---|
| SCK  | 10 | SPI1, shared by both devices |
| MISO | 8  | SPI1, shared |
| MOSI | 11 | SPI1, shared |
| Flash CS | 9  | dedicated chip-select, flash device |
| SD CS    | 13 | dedicated chip-select, SD device |
| SD Card-Detect | 15 | plain GPIO, active-low switch in the SD socket |

Recommended flash part: **W25Q128FV** (matches `USE_FLASH_W25Q128FV` in
`target.h`'s enabled flash driver list). SD slot: any push-push microSD
socket wired for SPI mode (SDIO is not implemented — PICO is SPI-only for
SD, per `USE_SDCARD_SPI`).

## 9. Motor outputs — DSHOT via PIO0

4 DSHOT-capable outputs, driven by PIO state machines (`PIO_DSHOT_INDEX =
0`), not hardware PWM — any GPIO works here, the choice below is purely for
clean board routing (grouped/contiguous), not a function-select constraint.

| Output | GPIO |
|---|---|
| M1 | 32 |
| M2 | 33 |
| M3 | 34 |
| M4 | 35 |

Wingflight's default fixed-wing mixer only uses M1 (throttle); the other 3
are for twin-engine / multi-motor wing layouts and bidirectional-DSHOT eRPM
telemetry (feeds `USE_RPM_FILTER`/`USE_DYN_NOTCH_FILTER`, both enabled).

## 10. Servo outputs — hardware PWM

8 servo channels, standard 50-330Hz PWM via RP2350's PWM slices
(`pwm_servo_pico.c`). **Unlike the DSHOT pins, these pin choices are not
arbitrary** — RP2350 PWM slice = `(gpio/2) mod 8`, channel = `gpio mod 2`,
so two GPIOs exactly 16 or 32 apart share the same slice+channel and cannot
be driven independently. The 8 pins below were checked pairwise against
that rule (all 8 map to distinct `(slice, channel)` pairs) — do not
substitute pins without re-checking.

| Output | GPIO | (slice, channel) |
|---|---|---|
| S1 | 4  | (2, A) |
| S2 | 12 | (6, A) |
| S3 | 14 | (7, A) |
| S4 | 24 | (4, A) |
| S5 | 25 | (4, B) |
| S6 | 29 | (6, B) |
| S7 | 39 | (3, B) |
| S8 | 43 | (5, B) |

Default fixed-wing mixer uses S1-S4 (left/right aileron, elevator, rudder);
S5-S8 cover flaps, a second aileron pair, thrust-vectoring actuators
(`BOXTHRUSTVECTOR`), or retracts.

## 11. Serial ports

| Port | Type | GPIO (TX/RX) | Suggested use |
|---|---|---|---|
| USB VCP | USB CDC | dedicated USB pins | Configurator / MSP |
| UART0 (hw, `UARTDEV_1`) | hardware | 16 / 17 | Receiver (SBUS today; CRSF once ported — wire both TX/RX) |
| UART1 (hw, `UARTDEV_2`) | hardware | 20 / 21 | GPS / telemetry |
| Softserial1 | PIO (`SOFTSERIAL1`) | 6 / 7 | Spare (MSP passthrough, secondary telemetry) |
| Softserial2 | PIO (`SOFTSERIAL2`) | 22 / 23 | Spare / future S.Port dual-pad workaround (see §2) |

UART0/UART1 pins were taken directly from `serial_uart_pico.c`'s
`uartHardware[]` candidate arrays (`UARTDEV_1` RX `{1,3,13,15,17,19,29,31,
33,35,45,47}` / TX `{0,2,12,14,16,18,28,30,32,34,44,46}`; `UARTDEV_2` RX
`{5,7,9,11,21,23,25,27,37,39,41,43}` / TX `{4,6,8,10,20,22,24,26,36,38,40,
42}`). Softserial pins are PIO-driven and unconstrained (any GPIO), 8N1
only, not limited to low baud rates unlike STM32 timer-based soft serial.

## 12. ADC — battery/current sensing

RP2350B's ADC only reads GPIO40-47 (`adc_pico.c`'s `adcChannelByPin()`:
`pin - 40` for RP2350B/RP2354B, vs. GPIO26-29 on the A-package). Only 4 of
the 4 usable slots (`ADC_BATTERY`/`ADC_CURRENT`/`ADC_RSSI`/`ADC_VBEC`) are
populated by the driver at once.

| Signal | GPIO |
|---|---|
| VBAT (battery voltage divider) | 40 |
| CURRENT (current sensor output) | 41 |
| VBEC (BEC rail monitor, optional) | 42 |

Analog RSSI is left unpopulated — most receivers report RSSI over telemetry
now; add a divider on GPIO43-47 later if a specific receiver needs it (note
GPIO43 is already claimed by Servo S8 in this design — use 44-47 instead).

## 13. LED strip / Buzzer

| Signal | GPIO | Driven by |
|---|---|---|
| LED strip (WS2812/WS2811) | 26 | PIO2 (`PIO_LEDSTRIP_INDEX = 2`) |
| Buzzer | 37 | Hardware PWM (`pwm_beeper_pico.c` uses `pwm_set_gpio_level()` for tone generation) |

GPIO37 for the buzzer is a deliberate choice, not arbitrary: `(37/2) mod 8
= 2`, channel B — checked against all 8 servo `(slice, channel)` pairs in
§10 to confirm no collision (the buzzer also drives a PWM slice, so it's
subject to the same ±16/±32 rule as servos).

## 14. Spare / expansion GPIO

10 pins are unused by this design and should be broken out to a header for
expansion (second IMU, rangefinder, pinioboxes, camera control, extra ADC
with RSSI, etc.): **GPIO 27, 28, 30, 31, 36, 38, 44, 45, 46, 47.**

## 15. Full pin map (master table)

| GPIO | Function |
|---|---|
| 0 | IMU MISO (SPI0) |
| 1 | IMU CS |
| 2 | IMU SCK (SPI0) |
| 3 | IMU MOSI (SPI0) |
| 4 | Servo S1 |
| 5 | IMU INT1 |
| 6 | Softserial1 TX |
| 7 | Softserial1 RX |
| 8 | Blackbox flash MISO (SPI1) |
| 9 | Blackbox flash CS |
| 10 | Blackbox flash / SD SCK (SPI1, shared) |
| 11 | Blackbox flash / SD MOSI (SPI1, shared) |
| 12 | Servo S2 |
| 13 | SD CS |
| 14 | Servo S3 |
| 15 | SD Card-Detect |
| 16 | RX UART0 TX |
| 17 | RX UART0 RX |
| 18 | Baro I2C1 SDA |
| 19 | Baro I2C1 SCL |
| 20 | GPS/Telem UART1 TX |
| 21 | GPS/Telem UART1 RX |
| 22 | Softserial2 TX |
| 23 | Softserial2 RX |
| 24 | Servo S4 |
| 25 | Servo S5 |
| 26 | LED strip |
| 27 | spare |
| 28 | spare |
| 29 | Servo S6 |
| 30 | spare |
| 31 | spare |
| 32 | Motor M1 (DSHOT) |
| 33 | Motor M2 (DSHOT) |
| 34 | Motor M3 (DSHOT) |
| 35 | Motor M4 (DSHOT) |
| 36 | spare |
| 37 | Buzzer |
| 38 | spare |
| 39 | Servo S7 |
| 40 | ADC VBAT |
| 41 | ADC CURRENT |
| 42 | ADC VBEC |
| 43 | Servo S8 |
| 44 | spare |
| 45 | spare |
| 46 | spare |
| 47 | spare |

Not in this table (dedicated RP2350B pins, not part of GPIO0-47): QSPI
flash bus, USB D+/D-, SWCLK/SWDIO, XIN/XOUT, RUN, and the ADC/core power
pins — see §4-5.

## 16. Bill of materials (reference selections)

| Part | Selection | Notes |
|---|---|---|
| MCU | RP2350B | QFN-80 |
| Boot flash | W25Q64JV (8MB QSPI) | matches `target.mk` default |
| Blackbox flash | W25Q128FV (SPI NOR) | separate physical chip from boot flash |
| microSD | any SPI-mode push-push socket | SPI only, no SDIO |
| IMU | Bosch BMI270 | reference driver, others auto-detect too |
| Barometer | DPS310 or BMP280 | optional, I2C |
| Crystal | 12MHz, load caps per datasheet | required, no internal RC fallback |
| USB | USB-C receptacle | VCP + MSC |
| Voltage regulator | 5V→3.3V buck/LDO + RP2350 internal VREG passives | verify VREG circuit against datasheet |

## 17. Open items before this is bring-up-ready

- [ ] Cross-check RP2350B QFN-80 power/ground pin list and decoupling
      values against the official datasheet (not re-derived here).
- [ ] Confirm RP2350's OTP-configurable BOOTSEL mechanism and wire
      accordingly — do not reuse an RP2040 boot-button reference circuit.
- [ ] Confirm the I2C1 SDA/SCL pin pair (GPIO18/19, §7) against the
      RP2350B datasheet's GPIO function table — `bus_i2c_pico.c` has no
      compiled-in candidate array to cross-check against, unlike SPI/UART.
- [ ] Verify RP2350 internal VREG (core regulator) external
      inductor/capacitor values.
- [ ] Once a board exists, generate the matching `.config` custom-defaults
      file (`resource`/`gyro_1_bustype`/`baro_hardware`/`pwm_output_mode`
      etc. per §15) so the firmware ships sane defaults for this design,
      following the `wingflight-targets` `.config` distribution model
      referenced in `target.h` (see `XLPOWER_F4MINI.config` on the `f4_ref`
      branch for the existing STM32 example of this file format).
