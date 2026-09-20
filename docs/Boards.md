# Flight controller hardware

Wingflight runs on STM32 flight controllers, using a small set of **unified targets**, one per
MCU family, rather than one firmware build per board:

| Target | MCU |
|---|---|
| `STM32F405` | STM32F405 |
| `STM32F7X2` | STM32F722 |
| `STM32F745` | STM32F745 |
| `STM32H743` | STM32H743 |
| `STM32F411` | STM32F411 (end of life, to be removed) |
| `STM32G47X` | STM32G474 (end of life, to be removed) |

The board-specific part (which pin is a servo output, where the gyro is, which UART is which) is
not in the firmware. It is a **board configuration** loaded on top of the unified target. The
board configurations live in the
[WingFlight/wingflight-targets](https://github.com/WingFlight/wingflight-targets) repository.
Wingflight inherits its supported boards from Rotorflight, which supports the boards Betaflight
4.3 does, as long as the board has enough suitable outputs for the servos and motors you need.

A board needs one output per servo plus one for each motor. A basic airplane needs four servos
(two ailerons, elevator, rudder) and one motor.

There is also a `SITL` target that builds Wingflight as a native program for simulation. See
`src/main/target/SITL/README.md`.

## Flashing

Flash with the
[Wingflight Configurator](https://github.com/WingFlight/wingflight-configurator/releases). See
[Installation](Installation.md) and [USB Flashing](USB%20Flashing.md).

## Custom hardware

See [Custom Board Configuration](Custom%20Board%20Configuration.md).
