# Pico 2 SD Card Test Setup

Bench configuration for testing SD-card blackbox logging on a Raspberry Pi
Pico 2 (RP2350A) running the `RP2350A` build of RP2350_UNIFIED.

It follows [RP235XB-Reference-Pinout.md](RP235XB-Reference-Pinout.md) where
the Pico 2 header allows it. The Pico 2 does not bring GPIO23, 24, 25 or 29
out to the header, so the reference SD-card bus (SPI1 on GPIO24-27) cannot
be used as-is.

## Wiring

| Function | GPIO | Pico 2 pin | Notes |
|---|---|---|---|
| SOFTSERIAL1 (loopback) | 4 | 6 | optional, only for the loopback test below |
| **SOFTSERIAL1 FBUS** | 5 | 7 | single wire to the receiver's FBUS pin (reference: GPIO22) |
| UART1_BIDIR_EN | 9 | 12 | bench choice (reference: GPIO30) |
| GYRO_INT | 11 | 15 | as reference |
| UART1 TX | 12 | 16 | as reference |
| UART1 RX | 13 | 17 | as reference |
| GYRO MISO | 16 | 21 | SPI0 RX, as reference |
| GYRO_CS | 17 | 22 | as reference |
| GYRO SCK | 18 | 24 | SPI0 SCK, as reference |
| GYRO MOSI | 19 | 25 | SPI0 TX, as reference |
| **SD_CS** | 22 | 29 | reference: GPIO25 (not on header) |
| **SD SCK** | 26 | 31 | SPI1 SCK, as reference |
| **SD MOSI** | 27 | 32 | SPI1 TX, as reference |
| **SD MISO** | 28 | 34 | SPI1 RX, reference: GPIO24 (not on header) |
| SD GND | GND | 28 or 38 | |
| SD VCC | 3V3(OUT) | 36 | use VBUS (pin 40) for 5V modules with an on-board regulator |

SD MISO has to be on GPIO28: the other SPI1 RX options are GPIO8 (motor 1),
GPIO12 (UART1 TX) and GPIO24 (not on header). SD_CS is a plain GPIO and can
move to any free pin.

The SD card takes the reference layout's SOFTSERIAL1 TX (GPIO22) and
SOFTSERIAL2 TX (GPIO28), so soft serial moves to other pins. Soft serial is a
PIO UART (pio1), which can use any GPIO, so this is only a matter of free
pins. Here SOFTSERIAL1 carries the FBUS receiver on GPIO5. Free header pins
for more ports: GPIO0, 1, 3, 4, 6, 7, 8, 10, 14, 15, 20, 21 (GPIO2 is
motor 1).

In half duplex (`serialrx_halfduplex = ON`, as FBUS uses) a soft serial port
uses its TX pin only, as a single wire. No jumper, switch or RX pin is needed.
The receiver can be on GPIO5 alone. It also works if GPIO4 is still
jumpered to GPIO5 from the loopback test, since GPIO4 is then an unused input.

A bare micro-SD socket needs 10k pull-ups to 3.3V on MISO and CS (and on the
unused DAT1/DAT2). Breakout modules usually have them already.

## CLI

Pins are given as `A<gpio>` (e.g. `A26` = GPIO26). Index 1 of `SPI_*` is
SPI0, index 2 is SPI1.

```
# gyro on SPI0
resource SPI_SCK 1 A18
resource SPI_MISO 1 A16
resource SPI_MOSI 1 A19
resource GYRO_CS 1 A17
resource GYRO_EXTI 1 A11
set gyro_1_bustype = SPI
set gyro_1_spibus = 1

# UART1
resource SERIAL_TX 1 A12
resource SERIAL_RX 1 A13
resource SERIAL_BIDIR_EN 1 A09

# SD card on SPI1
resource SPI_SCK 2 A26
resource SPI_MISO 2 A28
resource SPI_MOSI 2 A27
resource SDCARD_CS 1 A22
set sdcard_mode = SPI
set sdcard_spi_bus = 2
set blackbox_device = SDCARD

# FBUS receiver on SOFTSERIAL1 (serial port 30), single wire on GPIO5.
# Resource index 11 is SOFTSERIAL1, 12 is SOFTSERIAL2.
feature SOFTSERIAL
feature RX_SERIAL
feature TELEMETRY
resource SERIAL_TX 11 A05
serial 30 64 115200 57600 0 115200
set serialrx_provider = FBUS
set serialrx_inverted = ON
set serialrx_halfduplex = ON

save
```

The same settings as a paste-in file:
[Pico2-SDcard-Test.config](Pico2-SDcard-Test.config).

Without `feature SOFTSERIAL` the soft serial ports are left out even when
they have pins, and `serial` does not list port 30. If RX_SERIAL was on UART1
before, clear it there (`serial 0 0 115200 57600 0 115200`), since only one
port can have the RX function.

If a `resource` command reports a pin as already in use, free the old
assignment first with `resource <NAME> <index> NONE`.

## Checking

- `sd_info` shows the card state, size and filesystem (FAT32).
- `sd_bench [<MB>] [slow]` measures how long each block read/write keeps the
  card busy (histogram and worst case), in unused space of the log
  reservation. For logging the worst-case write should stay well below
  250ms. `slow` runs at a 4MHz SPI clock, to tell a slow card from a
  marginal SPI link. Remove `USE_SDCARD_BENCH` from the target to leave the
  command out of the build. Measured on this setup (16MB, full clock):

  | Card | Read | Single-block write | Multi-block write |
  |---|---|---|---|
  | no-name `SD16G` (mfr 0x27) | 31 kB/s, max 60ms | 74 kB/s, max 2431ms | 974 kB/s, max 165ms |
  | SanDisk `SB16G` (mfr 0x03) | 1475 kB/s, max 3.2ms | 516 kB/s, max 5.3ms | 2321 kB/s, max 6.3ms |

  The no-name card's second-long write stalls outlast the driver's write
  timeout, and the reset in the middle of a write leaves the card
  unresponsive until power cycled. The SanDisk logs at full rate cleanly.
- A card that has never been formatted (no boot signature in sector 0) is
  formatted as FAT32 automatically; `sd_info` shows `Formatting blank card`
  meanwhile (about 20s for a 16GB card). Cards with any existing partition
  table or volume are never touched, so a card formatted as exFAT or NTFS
  still has to be reformatted as FAT32 on a PC.
- Arm (with the default `blackbox_mode = NORMAL`) and check that a log file
  appears after disarming.
- With USB mass storage (`USE_USB_MSC`) the card can be read from the PC.

## Checking soft serial

- **FBUS:** RXLOSS is gone from the arming disable flags in `status`, and the
  receiver's channels show up (Receiver tab, or MSP_RC). Judge telemetry
  only outside CLI mode: the
  CLI stops the telemetry task, so in CLI the FC answers polls with null
  frames only. Measured with a Saleae MSO on GPIO5 (10s, 460800 inverted):
  control frame every 7.0ms, 0 framing errors in 64105 bytes, 979 of 979 FC
  replies with a valid checksum (including telemetry data frames), reply
  starting 520-850us after the poll.
- **Loopback:** remove the receiver, set `resource SERIAL_TX 11 A04` and
  `resource SERIAL_RX 11 A05`, and jumper GPIO4 to GPIO5. Then
  `serialpassthrough 30 <baud>` echoes back everything sent from a terminal.
  Measured 9600 baud to 12 Mbaud, byte-exact, with the bit time within
  0.01% of nominal up to 1 Mbaud. The echo tops out at about 115 kB/s. That
  is the passthrough loop over USB, not the port.
- `serialpassthrough 30 0 rxtx reset` follows the terminal's baud rate
  setting, and dropping DTR (closing the terminal) resets the board.
  Without `reset`, only a power cycle ends passthrough.
- **CPU load:** with FBUS running, the soft serial interrupts plus the FBUS
  byte parsing take about 1% of core 0 (sampled PC, 3.2kHz gyro loop, board
  idle otherwise). There is one interrupt per received byte, about 5000/s
  for FBUS. The 8-byte PIO RX FIFO gives the interrupt about 170us at
  460800 before bytes are lost.
