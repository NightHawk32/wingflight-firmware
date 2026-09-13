## SITL with JSBSim + FlightGear

This is the current, actively-maintained way to fly Wingflight's `SITL` target with
real flight dynamics: [JSBSim](https://jsbsim.sourceforge.net/) computes the physics
(by default `wingflight_3d_2m`, a generic 2 m 12S 3D aerobatic plane, see
[SITL 3D Aircraft Model](../../../docs/development/SITL%203D%20Aircraft%20Model.md);
JSBSim's bundled `c172p` still works with `-Aircraft c172p`), and
[FlightGear](https://www.flightgear.org/) optionally renders it. JSBSim and FlightGear
are the only supported simulator backends; the Gazebo 8 workflow inherited from
Betaflight has been removed.

For full details (toolchain install, protocol design, gotchas, and version pinning),
see [docs/development/SITL JSBSim FlightGear Plan.md](../../../docs/development/SITL%20JSBSim%20FlightGear%20Plan.md).

### Quick start

```powershell
# One-time, on a fresh clone (tools/ is gitignored, so nothing is there yet):
make mingw_sdk_install                                        # native C toolchain
.\scripts\sitl-jsbsim-flightgear-launch.ps1 -SetupVenv         # JSBSim/pygame venv

# Build SITL, start it, start the JSBSim bridge (c172p by default), and validate the
# full RC -> mixer -> JSBSim -> attitude loop end-to-end (signed response vs. a
# control-neutral drift baseline):
.\scripts\sitl-rc-check.ps1 -Mode jsbsim -BuildSitl -AutoStartSitl -StopSitlOnExit

# Same, for the two paths the disarmed check can't cover: a real ARMED throttle
# run, and the GPS feed (bridge --msp-gps -> MSP on TCP 5762 -> gps.c):
.\scripts\sitl-rc-check.ps1 -Mode throttle -AutoStartSitl -StopSitlOnExit
.\scripts\sitl-rc-check.ps1 -Mode gps -AutoStartSitl -StopSitlOnExit -FreshEeprom

# For interactive flying/visualization instead of a one-shot validation run, use the
# JSBSim + FlightGear launcher (-Joystick needs a USB joystick; without an RC source
# the mixer just sits at failsafe. FlightGear is optional and not vendored - install
# it manually, see the plan doc's install instructions; -SetupFgAircraft once for the
# 2 m Edge 540 visual):
.\scripts\sitl-jsbsim-flightgear-launch.ps1 -BuildSitl -Trim -Joystick -FlightGear `
    -FgfsPath "C:\Program Files\FlightGear 2024.1\bin\fgfs.exe" -StopOnExit
```

If a run looks dead (`packets_rx=0` in the bridge output), check that no leftover
process still holds the simulator's UDP ports: `Get-NetUDPEndpoint -LocalPort 9002`.
And delete any old `obj/main/wingflight_SITL.exe` - `make` builds
`wingflight_SITL.elf`, and a stale `.exe` beside it is a well-worn trap.

Key pieces:
- [scripts/jsbsim_bridge.py](../../../scripts/jsbsim_bridge.py) — Python bridge:
  receives Wingflight's `servo_packet` (control surfaces + motor speed) over UDP,
  drives the corresponding JSBSim FCS properties, steps the simulation, and sends
  JSBSim's resulting state back as an `fdm_packet` (see Ports below). Optional `--flightgear` flag additionally
  makes JSBSim emit its own native FlightGear UDP FDM stream for visualization.
- [scripts/sitl-jsbsim-flightgear-launch.ps1](../../../scripts/sitl-jsbsim-flightgear-launch.ps1) —
  one-shot launcher for SITL + the bridge + (optionally) FlightGear.
- [scripts/sitl-rc-check.ps1](../../../scripts/sitl-rc-check.ps1) `-Mode jsbsim` —
  drives roll/pitch RC to each extreme through the disarmed mixer-passthrough
  override and asserts JSBSim's resulting `MSP_ATTITUDE` actually changes, proving
  the entire simulation loop (RC → mixer → bridge → JSBSim physics → fake IMU → MSP)
  is alive, not just Wingflight's own servo output.
- To configure/tune SITL from the Wingflight Configurator (it connects over MSP on
  its own port, `tcp://127.0.0.1:5763`, so it runs alongside the joystick; the
  launcher's `-Configurator` switch starts it for you), see
  [docs/development/SITL Configurator Connection.md](../../../docs/development/SITL%20Configurator%20Connection.md).
- For manual/interactive control (USB joystick/gamepad instead of the automated RC
  checks above), see the joystick section below.

Pinned versions that are known to work together (JSBSim, MinGW-w64, Python venv) are
recorded in the plan doc's §9 — update them there if you upgrade any component.

### Flying with a USB joystick/gamepad
For a documented, ready-to-use tool that reads a USB joystick/gamepad and feeds it
into SITL as RC input (with a GUI for binding axes/buttons to RC channels), see
[docs/development/SITL Joystick RC Input.md](../../../docs/development/SITL%20Joystick%20RC%20Input.md)
and [scripts/sitl-joystick-rc.py](../../../scripts/sitl-joystick-rc.py). The launcher's
`-Joystick` switch starts it for you.

### Ports
| Direction | Address | Payload |
|---|---|---|
| Wingflight -> JSBSim bridge | `udp://127.0.0.1:9002` | `servo_packet`: `motor_speed[4]` (M1-M4) + `servo[8]` (S1-S8, us) |
| JSBSim bridge -> Wingflight | `udp://127.0.0.1:9003` | `fdm_packet`: IMU, attitude quaternion, velocity, NED position |
| JSBSim -> FlightGear | `udp://127.0.0.1:5550` | FlightGear native FDM (only with `--flightgear`) |
| UARTx <-> MSP clients | `tcp://127.0.0.1:576x` | one client per port; 5761 = RC client (joystick), 5762 = bridge `--msp-gps`, 5763 = Configurator |

Both structs are defined in [target.h](target.h). `motor_speed[i]` carries M(i+1),
so the wing throttle M1 is `motor_speed[0]`.

### eeprom.bin
`eeprom.bin` (in the working directory SITL is started from) holds the saved
config. Its size is `EEPROM_SIZE` (32768 bytes) in
[src/main/target/SITL/target.h](target.h). Note: on a fresh/missing `eeprom.bin`,
SITL's first launch writes the default config and exits - run it again. A stale
`eeprom.bin` saved by an older build can also mask config-default fixes (e.g.
report `servoCount=0` forever); delete it or use `sitl-rc-check.ps1 -FreshEeprom`
to re-exercise the defaults.

"Save and reboot" from the Configurator writes `eeprom.bin` and then **exits** SITL
(there is no in-process restart), so start it again yourself. Features SITL doesn't
compile in (LED_STRIP, OSD, SOFTSERIAL, RANGEFINDER, DYN_NOTCH, RPM_FILTER, ...) are
cleared when you save. SITL prints `[config] features 0x... not supported by this build`
on stderr when that happens.
