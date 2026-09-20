# Firmware docs

User-facing documentation lives in the
[wingflight-docs](https://github.com/WingFlight/wingflight-docs) repository, published at
<https://doc.wingflight.org>. That repository is the source of truth. This directory keeps only design and
developer material that belongs next to the code. See [AGENTS.md](../AGENTS.md) for the working rules.

## Design and rationale

| File | What it is |
|---|---|
| [FlightDynamics.md](FlightDynamics.md) | Flight-control signal chain, sign conventions, the rationale behind each stage, and a review of known defects with a test plan |
| [SmartFuel.md](SmartFuel.md) | How the SmartFuel charge estimator works, its parameters and the MSP/telemetry interface |
| [rx-wiring-autodetect-design.md](rx-wiring-autodetect-design.md) | Design of the RX serial wiring auto-detect (referenced from the source) |
| [esc-signaling-autodetect-design.md](esc-signaling-autodetect-design.md) | Design of ESC telemetry signalling auto-detect |
| [EXST bootloader.md](EXST%20bootloader.md) | The external-storage bootloader model |
| [API/MSP_extensions.md](API/MSP_extensions.md) | MSP extension notes. The message list itself is `src/main/msp/msp_protocol*.h` |

## Development

| File | What it is |
|---|---|
| [development/Development.md](development/Development.md) | Contribution guidelines and testing practice |
| [development/CodingStyle.md](development/CodingStyle.md) | Coding style |
| [development/ParameterGroups.md](development/ParameterGroups.md) | The parameter-group pattern used to store and transfer configuration |
| [development/Configuration Format.md](development/Configuration%20Format.md) | The configuration format and how it maps to the external protocol |
| [development/Configuration Storage.md](development/Configuration%20Storage.md) | Where configuration is stored in flash, and how to erase it |
| [development/Blackbox Internals.md](development/Blackbox%20Internals.md) | Blackbox log format |
| [development/Atomic Barrier.md](development/Atomic%20Barrier.md) | The atomic-barrier implementation |
| [development/TestCoverage.md](development/TestCoverage.md) | Measuring unit-test coverage |
| [development/Hardware Debugging.md](development/Hardware%20Debugging.md), [with VS Code and J-Link](development/Hardware%20Debugging%20with%20VSCode%20and%20JLink.md) | Debugging on real hardware |
| [development/Custom Board Configuration.md](development/Custom%20Board%20Configuration.md) | Mapping pins on a custom board with the CLI |
| [development/Customized Version.md](development/Customized%20Version.md) | Building a firmware with different features enabled |

## User-facing pages still to move

These describe subjects that the wingflight-docs site does not cover yet. They stay here until they are ported
there, and then should be deleted from this repository.

| File | Subject |
|---|---|
| [Controls.md](Controls.md) | Stick commands and arming |
| [Safety.md](Safety.md) | Bench and first-flight safety |
| [Rx.md](Rx.md), [Rssi.md](Rssi.md), [Spektrum bind.md](Spektrum%20bind.md), [FrSky SPI RX.md](FrSky%20SPI%20RX.md) | Receiver protocols, RSSI and binding |
| [Serial.md](Serial.md) | Serial port functions and passthrough |
| [Telemetry.md](Telemetry.md), [CastleESCTelemetry.md](CastleESCTelemetry.md) | Telemetry protocols and Castle Live Link |
| [Gps.md](Gps.md) | GPS setup, u-blox configuration, RTH and loiter settings |
| [VTX.md](VTX.md) | VTX control and the VTX table |
| [LedStrip.md](LedStrip.md), [Buzzer.md](Buzzer.md), [Display.md](Display.md) | LED strip, buzzer and OLED display |
