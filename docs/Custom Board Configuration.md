## Custom Board Configuration Using CLI

Warning: this is beyond the normal use case. Use a pre-made board configuration for a flight
controller that has been tested, from the
[wingflight-targets](https://github.com/WingFlight/wingflight-targets) repository.

Wingflight can support a custom flight controller, as long as it uses supported hardware. Before
you start, check that your MCU and peripherals are supported, and test your hardware separately.
Then flash the matching unified target (see [Boards](Boards.md)) and configure the pins from the
CLI.

Use the `resource` command to map each peripheral to a pin:

- `# resource` lists the current assignments
- `resource <resource> [<index>] <pin>` assigns one, for example `resource SERVO 1 B0`
- `resource <resource> [<index>] none` frees it

The `timer` and `dma` commands attach a timer or DMA channel to a pin. Servo and motor outputs,
and some other functions such as battery voltage monitoring, need this:

- `# timer`
- `timer <pin> AF(x) [1-3]`
- `# dma`
- `dma [SPI_TX|ADC|pin] [index|pin] [0|1]`

Use `save` to store the configuration. For anything beyond this, use the pre-made board
configurations as a reference.
