# Connecting the Configurator to SITL

This documents how to attach the [Wingflight
Configurator](https://github.com/WingFlight/wingflight-configurator) to a running
`TARGET=SITL` build, so you can configure, tune and inspect the simulated flight
controller with the same GUI you would use on real hardware.

It complements the two script-driven workflows:
[SITL Joystick RC Input.md](SITL%20Joystick%20RC%20Input.md) (manual flying) and
[SITL JSBSim FlightGear Plan.md](SITL%20JSBSim%20FlightGear%20Plan.md) (physics +
visualization). The configurator is a *third* MSP client and has to share ports
with those — see [Port budget](#port-budget-only-one-client-per-port) below, that
is the part people trip over.

## How it works

SITL has no USB. Instead, every UART is exposed as a TCP listening socket on
localhost (see `BASE_PORT` in
[src/main/drivers/serial_tcp.c](../../src/main/drivers/serial_tcp.c#L47)):

```
UART1 -> tcp://127.0.0.1:5761
UART2 -> tcp://127.0.0.1:5762
UARTn -> tcp://127.0.0.1:576(n)      (SERIAL_PORT_COUNT = 8, so up to 5768)
```

The configurator's MSP layer is transport-agnostic: its "manual" port entry
accepts a `tcp://host:port` address and dials it with `chrome.sockets.tcp`
instead of `chrome.serial` (see `connectTcp()` in the configurator's
`src/js/serial.js`). MSP then runs over that socket exactly as it would over a
serial link, so all the normal tabs work.

Which of those TCP ports actually speak MSP is decided by the firmware's serial
config. On a fresh SITL config that is:

- **UART1 / 5761** — MSP, from the generic default
  (`serialConfig->portConfigs[0].functionMask = FUNCTION_MSP` in
  [src/main/io/serial.c](../../src/main/io/serial.c#L143)).
- **UART2 / 5762** — MSP, from SITL's target config
  ([src/main/target/SITL/config.c](../../src/main/target/SITL/config.c)), added
  so `jsbsim_bridge.py --msp-gps` has a port of its own.

Baud rate is irrelevant over TCP — the configurator's baud selector is ignored
for a `tcp://` connection.

## Requirements

- A SITL build: `make TARGET=SITL` (see the
  [SITL README](../../src/main/target/SITL/README.md) for the one-time toolchain
  setup, `make mingw_sdk_install` on Windows).
- The configurator running as a **desktop (NW.js) build**, not the web build.
  The manual `tcp://` entry is deliberately hidden under `__BACKEND__ === "web"`
  (see `src/js/port_handler.js`) because browsers have no raw TCP. From a
  configurator checkout: `make init` once, then `pnpm start`.

## Quick start

1. **Start SITL.** Either bare:

   ```powershell
   .\obj\main\wingflight_SITL.elf
   ```

   or, more usefully, together with the physics bridge:

   ```powershell
   .\scripts\sitl-jsbsim-flightgear-launch.ps1 -Trim -StopOnExit
   ```

   On a fresh/missing `eeprom.bin`, the first launch writes the default config
   and exits — just start it again.

   Confirm the ports are up; SITL prints one line per UART on stderr:

   ```
   bind port 5761 for UART1
   bind port 5762 for UART2
   ```

2. **Start the configurator** (`pnpm start` in the configurator checkout).

3. In the port picker (top right), choose **Manual**. A text field appears next
   to it — replace its contents with:

   ```
   tcp://127.0.0.1:5761
   ```

4. Click **Connect**. SITL logs `[NEW]UART1` on stderr when the socket is
   accepted, and the configurator should land on the Setup tab with a live
   attitude indicator.

The manual address is persisted (`portOverride` in the configurator's config
store) and a `tcp` address is re-selected automatically on the next start, so
step 3 is a one-time thing.

## Port budget: only one client per port

**This is the main gotcha.** SITL's TCP serial driver uses one accept thread per
port and serves exactly one client at a time: `tcpAcceptThread()` in
[src/main/drivers/serial_tcp.c](../../src/main/drivers/serial_tcp.c#L62) does not
return to `accept()` until the current client's `recv()` loop ends. A second
client's `connect()` still succeeds (the OS listen backlog takes it), so the
configurator will look "connected" and then time out with no MSP replies at all,
which reads like a firmware bug and is not one.

So the tools have to be divided across ports:

| Client | Default port | Override flag |
|---|---|---|
| `scripts/sitl-joystick-rc.py` | 5761 (UART1) | `--port` / `--port-candidates` |
| `scripts/sitl-rc-check.ps1` | 5761 (UART1) | `-PortCandidates` |
| `scripts/jsbsim_bridge.py --msp-gps` | 5762 (UART2) | `--msp-gps-port` |
| Configurator | your choice | the manual `tcp://` address |

Pick one of these:

- **Configurator only** (no joystick, no RC check): use `tcp://127.0.0.1:5761`.
- **Configurator + joystick, no GPS feed**: leave the joystick on 5761 and give
  the configurator `tcp://127.0.0.1:5762`.
- **Configurator + joystick + `--msp-gps`**: all three need a port, so open a
  third one. In the configurator CLI (or any MSP client), enable MSP on UART3:

  ```
  serial 2 1 115200 57600 0 115200
  save
  ```

  (`2` = `SERIAL_PORT_USART3`, `1` = `FUNCTION_MSP`; see
  [src/main/io/serial.h](../../src/main/io/serial.h#L88).) After restarting SITL,
  connect the configurator to `tcp://127.0.0.1:5763` and keep 5761/5762 for the
  joystick and the GPS feed. Note this is a *saved config* change, so it lives in
  `eeprom.bin` and is lost if you delete it or run with `-FreshEeprom`.

Freeing a port is just disconnecting the client that holds it — SITL's accept
thread picks up the next waiting client immediately (`[CLS]UART1` then
`[NEW]UART1` on stderr).

## Things that behave differently from real hardware

- **"Save and Reboot" exits SITL.** `systemResetHard()` in
  [src/main/target/SITL/target.c](../../src/main/target/SITL/target.c#L349) calls
  `exit(0)` — there is no reboot, the process just terminates and the
  configurator drops the connection. The config *is* written to `eeprom.bin`
  first, so you only need to restart SITL (and reconnect) for it to take effect.
  With `sitl-jsbsim-flightgear-launch.ps1 -StopOnExit`, SITL exiting also takes
  the bridge and FlightGear down with it.
- **Config lives in `eeprom.bin`** in the directory SITL was started from, not on
  a chip. Deleting it (or `sitl-rc-check.ps1 -FreshEeprom`) resets everything the
  configurator saved. Conversely, a stale `eeprom.bin` from an older build keeps
  its stored values and will mask changed config defaults — if a setting refuses
  to look like the default you expect, that is usually why.
- **Firmware flashing / DFU does not apply.** Rebuild with `make TARGET=SITL`
  instead; the Firmware Flasher tab is meaningless here.
- **RX is MSP.** `DEFAULT_RX_FEATURE` is `FEATURE_RX_MSP`
  ([target.h](../../src/main/target/SITL/target.h#L89)), so the Receiver tab shows
  channels only while something is streaming `MSP_SET_RAW_RC` — i.e. while
  `sitl-joystick-rc.py` or `sitl-rc-check.ps1` is running on its own port. With no
  RC source the bars sit at failsafe values. The Receiver tab and the RC source
  are then two different MSP clients on two different ports; that is expected.
- **No real sensors.** The IMU/baro values the configurator shows come from the
  simulator (JSBSim state arriving as the bridge's `fdm_packet`), and the GPS fix
  appears only if the bridge runs with `--msp-gps` and the config's GPS provider
  is still `MSP` — see [SITL/config.c](../../src/main/target/SITL/config.c).
- **Motors/servos are simulated.** The Motors tab drives the same outputs the
  bridge forwards to JSBSim, so moving a slider there really does move the
  simulated aircraft. Nothing is physically dangerous, but the aircraft reacts.

## Troubleshooting

| Symptom | Cause |
|---|---|
| No **Manual** entry in the port picker | You are running the web build; use the desktop/NW.js one (`pnpm start`). |
| Connect hangs, then "no configurator response" | Another client already holds that port (see [Port budget](#port-budget-only-one-client-per-port)). Check SITL's stderr for a matching `[NEW]UARTn`. |
| Connection refused | SITL isn't running, or you used a port whose UART has no `FUNCTION_MSP` (only 5761/5762 do by default), or SITL exited on its first-run `eeprom.bin` write — start it again. |
| Configurator connects but everything reads zero/failsafe | Normal with no RC source and no physics bridge attached; start the bridge and/or `sitl-joystick-rc.py`. |
| Settings revert after restart | You didn't **Save**, or `eeprom.bin` was deleted/regenerated between runs. |
| Connection drops right after Save | Expected — the reboot is an `exit(0)`. Restart SITL. |
