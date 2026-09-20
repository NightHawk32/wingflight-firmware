# Configuration

Wingflight is configured mainly with the
[Wingflight Configurator](https://github.com/WingFlight/wingflight-configurator). The Configurator
and the command line interface (CLI) are both reached by connecting to a serial port on the
board, whether that is the USB virtual serial port, or a hardware UART. See [Serial](Serial.md).

The Configurator cannot configure every setting. Some features and settings can only be enabled
or changed from the CLI. See [Cli](Cli.md).

**Back up your settings with the CLI `dump` command before upgrading the firmware or the
Configurator**, so that you can re-apply them if something changes or is reset.

The Configurator also has a terminal tab that talks to the CLI.

Settings that you tune together are grouped into profiles. See [Profiles](Profiles.md).
Transmitter-side tuning is covered in [Inflight Adjustments](Inflight%20Adjustments.md).
