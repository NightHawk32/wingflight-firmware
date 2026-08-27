# RP235XB Reference Pinout

A GPIO0-47 reference pin assignment for an RP2350B/RP2354B (QFN-80, 48 GPIO)
flight controller: 4 UARTs, 2 DSHOT motors, an RPM input, gyro SPI +
interrupt, a shared SD-card/flash SPI bus, baro I2C, 8 servo channels, 2
status LEDs, 3 power-rail ADC inputs, SWD debug, and external QSPI program
flash + PSRAM.

This is a *reference* layout, not a fixed requirement: chip-selects,
interrupt lines and LED pins are plain GPIO and can move freely. Signals tied
to a specific peripheral mux (SPI/UART/I2C/PWM instance) are noted as such,
since those are constrained by the RP2350 hardware function-select table.

RP2350_UNIFIED is a runtime-configured target (see
`src/main/target/RP2350_UNIFIED/target.h`) - every pin below is applied via
CLI `resource`/`set` commands against a `.config` file, not board-specific
target code.

## GPIO0-47

| GPIO | Function | Mux | Notes |
|---|---|---|---|
| 0 | SERVO 1 | PWM0 A | direct PWM, no PIO |
| 1 | SERVO 2 | PWM0 B | |
| 2 | SERVO 3 | PWM1 A | |
| 3 | SERVO 4 | PWM1 B | |
| 4 | SERVO 5 | PWM2 A | |
| 5 | SERVO 6 | PWM2 B | |
| 6 | SERVO 7 | PWM3 A | |
| 7 | SERVO 8 | PWM3 B | |
| 8 | MOTOR 1 | PIO0 sm | bidir DSHOT |
| 9 | MOTOR 2 | PIO0 sm | bidir DSHOT |
| 10 | RPM_IN | GPIO IRQ | external tach / hall |
| 11 | GYRO_INT | GPIO IRQ | data-ready |
| 12 | UART1 TX | UART0 | hardware |
| 13 | UART1 RX | UART0 | hardware |
| 14 | BARO SDA | I2C1 | |
| 15 | BARO SCL | I2C1 | |
| 16 | GYRO MISO | SPI0 RX | |
| 17 | GYRO_CS | SIO | soft CS |
| 18 | GYRO SCK | SPI0 SCK | |
| 19 | GYRO MOSI | SPI0 TX | |
| 20 | UART2 TX | UART1 | hardware |
| 21 | UART2 RX | UART1 | hardware |
| 22 | SOFTSERIAL1 TX | PIO1 sm | bit-banged |
| 23 | SOFTSERIAL1 RX | PIO1 sm | bit-banged |
| 24 | STORAGE MISO | SPI1 RX | shared bus |
| 25 | SD_CS | SIO | soft CS |
| 26 | STORAGE SCK | SPI1 SCK | shared bus |
| 27 | STORAGE MOSI | SPI1 TX | shared bus |
| 28 | SOFTSERIAL2 TX | PIO1 sm | bit-banged |
| 29 | SOFTSERIAL2 RX | PIO1 sm | bit-banged |
| 30 | spare | | |
| 31 | spare | | |
| 32 | spare | | |
| 33 | FLASH_CS | SIO | soft CS |
| 34 | STATUS LED 1 | SIO | |
| 35 | STATUS LED 2 | SIO | |
| 36 | spare | | |
| 37 | spare | | |
| 38 | spare | | |
| 39 | spare | | |
| 40 | ADC_VBAT | ADC0 | battery voltage |
| 41 | ADC_VBUS | ADC1 | USB/input rail |
| 42 | ADC_VBEC | ADC2 | BEC rail |
| 43 | spare (ADC3) | | |
| 44 | spare (ADC4) | | |
| 45 | spare (ADC5) | | |
| 46 | spare (ADC6) | | |
| 47 | PSRAM_CS1 | XIP_CS1 | one of only GPIO0/8/19/47 |

## Dedicated pins - not part of GPIO0-47

SWD and the program-memory QSPI bus sit on separate physical pins outside
the GPIO0-47 numbering entirely (same silicon area as the Pico's own debug
header and boot flash), so none of them cost a GPIO.

| Signal | Pin | Notes |
|---|---|---|
| SWCLK | dedicated | SWD clock, internal ~60kOhm pull-up |
| SWDIO | dedicated | SWD data, internal ~60kOhm pull-up |
| SWD GND | dedicated | pin 2 of a standard 3-pin debug header |
| QSPI_SCLK | dedicated | program-flash + PSRAM shared clock |
| QSPI_SS | dedicated | flash CS - QMI chip-select 0 |
| QSPI_SD0-SD3 | dedicated | 4 data lines, shared flash + PSRAM |
| PSRAM_CS1 | GPIO47 | QMI chip-select 1, see GPIO table above |

## Function groups

| Group | Detail | Pins |
|---|---|---|
| Servo | PWM out | 8 |
| Motor | DSHOT / PIO0 | 2 |
| RPM | tach input | 1 |
| UART | 2 hw + 2 PIO, full duplex | 8 |
| SPI0 | gyro, dedicated bus | 5 |
| SPI1 | SD + flash, shared bus | 5 |
| I2C1 | baro | 2 |
| LED | status, discrete GPIO | 2 |
| ADC | VBAT / VBUS / VBEC | 3 |
| SWD | debug, dedicated pins | not GPIO |
| QSPI | program flash + PSRAM | 6 dedicated + GPIO47 |

## PIO / peripheral budget

| Resource | Usage |
|---|---|
| PIO0 - DSHOT (4 sm max) | 2 / 4 used |
| PIO1 - soft serial (4 sm max) | 4 / 4 used |
| PIO2 - LED strip | reserved, unused |
| SPI peripherals | 2 / 2 used |
| I2C peripherals | 1 / 2 used |
| UART hardware | 2 / 2 used |
| ADC channels (8 avail.) | 3 / 8 used, 1 lost to PSRAM_CS1 |
| GPIO0-47 | 37 / 48 used |

## Design notes

- **GPIO0-29 carry over unchanged** from the RP2350A layout - servos,
  motors, RPM, gyro int, both hardware UARTs, baro I2C and both SPI buses
  sit exactly where they did. The extra 18 GPIOs on the B package (30-47)
  only add pins, they don't move anything.
- **SPI1 stays shared** between SD card and external flash, since the two
  are never accessed at once. SCK/MOSI/MISO in common, `SD_CS` (GPIO25) and
  `FLASH_CS` (GPIO33) as separate chip-selects so either device can be
  addressed independently on the one bus.
- **SOFTSERIAL2 is full duplex** (GPIO28/29) - the extra GPIO budget on the
  B package removes the RX-only compromise the A-package layout needed.
- **ADC0-2 (GPIO40-42)** read VBAT, VBUS and VBEC through external divider
  networks sized for each rail's voltage range. ADC3-6 (GPIO43-46) are left
  spare for a current sensor or a second battery input; ADC7 (GPIO47) is
  spent on PSRAM_CS1 instead.
- **Status LEDs are discrete GPIO** (GPIO34/35, active-low recommended),
  independent of the WS2812 LED-strip PIO output reserved on PIO2.
- **SWD and the program-memory QSPI bus are all dedicated pins.** RP2350B
  has no on-die flash (unlike RP2354B), so this external QSPI flash is
  mandatory for program memory, not optional storage.
- **PSRAM shares that same QSPI bus** (SCLK + SD0-3) as a second QMI
  chip-select, CS1. The RP2350's QMI hardware only routes CS1 to one of
  GPIO0, 8, 19 or 47, and GPIO47 is the only one of those four not already
  committed to a servo/motor/UART function in this layout, so it's the
  natural pick (configured via `GPIO_FUNC_XIP_CS1`, not as a plain GPIO).
- **11 GPIOs are still spare** (30-32, 36-39, 43-46) for a buzzer, boot
  button, a second I2C device, or expansion. Every mux-fixed signal
  (SPI/UART/I2C/PWM) here still respects the RP2350 hardware function
  table; chip-selects, interrupt and LED lines are plain GPIO and can move
  freely.
