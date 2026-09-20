# Firmware docs

Documentation lives in the [wingflight-docs](https://github.com/WingFlight/wingflight-docs) repository,
published at <https://doc.wingflight.org>. That includes the user guides, the CLI reference, and the developer
material under Contributing: code guidelines, coding style, and the
[Technical Reference](https://doc.wingflight.org/contributing/tech/) (parameter groups, configuration format,
Blackbox log format, SmartFuel internals, hardware debugging).

This directory keeps only design notes that need to sit next to the code they describe:

| File | What it is |
|---|---|
| [FlightDynamics.md](FlightDynamics.md) | The flight-control signal chain, sign conventions, the rationale behind each stage, and a review of known defects with a test plan. [AGENTS.md](../AGENTS.md) points here |
| [rx-wiring-autodetect-design.md](rx-wiring-autodetect-design.md) | Design of the RX serial wiring auto-detect. Referenced from source comments |
| [esc-signaling-autodetect-design.md](esc-signaling-autodetect-design.md) | The same mechanism applied to the ESC telemetry port |

Add a new document here only if it is tied to specific code and is referenced from it. Everything else belongs in
wingflight-docs. See [AGENTS.md](../AGENTS.md) for the working rules.
