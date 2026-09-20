# Installation

## Using the configurator

Flash Wingflight with the
[Wingflight Configurator](https://github.com/WingFlight/wingflight-configurator/releases). Flashing
with any other tool is not recommended.

1. Connect the flight controller to the PC.
2. Start the Configurator. If it connects to the board automatically, click "Disconnect".
3. Open the "Firmware Flasher" tab.
4. Choose your board and the firmware version, and read the release notes shown. When upgrading,
   read the notes for every release since your current one.
5. If this is the first time Wingflight is going on the board, tick "Full Chip Erase".
6. Select the correct serial port and click "Flash Firmware".
7. When the progress bar is green and reads "Programming: SUCCESSFUL", you are done.

Some boards must be flashed in USB DFU mode. See [USB Flashing](USB%20Flashing.md).

## Upgrading

Before upgrading, back up your settings with the CLI `dump` command (see [Cli](Cli.md)). Some
releases are not backwards compatible and reset settings to their defaults. After an upgrade,
compare the new defaults against your backup before restoring it.

## Building from source

See the build guides in [development](development/Building.md).
