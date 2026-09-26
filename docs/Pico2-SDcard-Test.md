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

This uses the reference layout's SOFTSERIAL1 TX (GPIO22) and SOFTSERIAL2 TX
(GPIO28), so soft serial is not available in this setup.

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

save
```

If a `resource` command reports a pin as already in use, free the old
assignment first with `resource <NAME> <index> NONE`.

## Checking

- `sd_info` shows the card state, size and filesystem (FAT32).
- Arm (with the default `blackbox_mode = NORMAL`) and check that a log file
  appears after disarming.
- With USB mass storage (`USE_USB_MSC`) the card can be read from the PC.
