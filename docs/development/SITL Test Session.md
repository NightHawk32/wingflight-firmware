# SITL Test Session (2026-09-13)

Guided walk-through of everything implemented for SITL so far (JSBSim +
FlightGear, joystick RC, Configurator connection). Issues found go into the
list at the bottom and are worked through after the session.

Environment: Windows 11, vendored MinGW-w64 (`tools/mingw64`), JSBSim venv
(`tools/jsbsim-venv`), FlightGear 2024.1 at `C:\Program Files\FlightGear 2024.1`.

## Test plan

| # | Area | How | Who | Result |
|---|---|---|---|---|
| 1 | Clean build | `mingw32-make TARGET=SITL DEBUG=GDB` from a clean `obj/main` | Claude | PASS (see I-2) |
| 2 | Automated checks | `sitl-rc-check.ps1` modes `smoke`, `sweep`, `stress`, `jsbsim`, `throttle`, `gps` (first with `-FreshEeprom`) | Claude | PASS all six (see I-1) |
| 3 | Bare SITL start | Run `wingflight_SITL.elf` alone: first-boot eeprom write, UART port binds | User | PASS (start via `$env:PATHEXT += ";.ELF"`) |
| 4 | Configurator | Connect over `tcp://127.0.0.1:5763` (was 5761, see I-6), browse tabs, save/"reboot" | User | PASS after I-3/I-4/I-6 (I-5 open) |
| 5 | Joystick RC | `sitl-joystick-rc.py` GUI, channel mapping, Receiver tab shows sticks | User | PASS |
| 6 | Bridge + joystick flight | Launcher `-Trim -Joystick`, arm, fly by bridge status line | User | PASS (c172p) |
| 6a | 3D model automated | `sitl-rc-check.ps1 -BridgeAircraft wingflight_3d_2m`, modes `jsbsim` (x2), `throttle`, `gps`; plus `jsbsim` on c172p as regression | Claude | PASS all (see I-7) |
| 6b | 3D model flight | Launcher `-Trim -Joystick -Configurator` with the new default `wingflight_3d_2m`: hover, knife-edge, rolls, torque roll | User | Pending (offline JSBSim validation done, see [SITL 3D Aircraft Model](SITL%203D%20Aircraft%20Model.md)) |
| 7 | FlightGear | Launcher `-Trim -Joystick -FlightGear -FgfsPath ...`, visual check | User | |
| 8 | Configurator + joystick + GPS together | Port budget scenario (5761/5762/5763) | User | |
| 9 | Error handling | Leftover process on 9002, stale `.exe`, bridge killed mid-run | Both | |

## Issues found

| ID | Test | Severity | Description | Status |
|---|---|---|---|---|
| I-1 | 2 | Medium | Initial latitude is set as geocentric (`ic/lat-gc-deg` in `jsbsim_bridge.py`) but GPS and FlightGear report geodetic. The aircraft starts at geodetic 37.80 N instead of KSFO's 37.61 N, about 21 km north. The `-Mode gps` check still passes because it only checks the fix is "near" the IC. The launcher also pre-positions FlightGear at 37.6136, so scenery preloads at the wrong spot. In FlightGear the aircraft appeared "in the middle of nowhere". **Fix:** the bridge sets `ic/lat-geod-deg`, and JSBSim's terrain elevation is set to KSFO's 13 ft. The default heading is 297.9° (along 28R), the 3D model starts at 200 ft MSL, and FlightGear opens in Chase view (`-FgView`). The `-Mode gps` near-IC tolerance was tightened from 0.5° to 0.02°. Verified: GPS fix 37.61388 / −122.35782, 52 m. | Fixed |
| I-2 | 1 | Low | `make TARGET=SITL clean` leaves the month-old `obj/main/wingflight_SITL.exe` behind, so every check prints the "Ignoring stale .exe" warning. Docs tell people to delete it by hand. | Open |
| I-3 | 4 | Medium | Feature enabled in the Configurator, then save + reboot: the feature is gone after restart. Saving works (supported features such as CMS persist). But the Configurator offers every feature, and `validateAndFixConfig()` clears the ones SITL compiles out (LED_STRIP, OSD, DASHBOARD, SOFTSERIAL, RANGEFINDER, DYN_NOTCH, RPM_FILTER, ...) with no message. **Fix:** SITL now prints `[config] features 0x... not supported by this build ... disabled, not saved` on stderr. Hiding unsupported features in the Configurator is still open. | Fixed (firmware side) |
| I-4 | 4 | Low | The repo-root `eeprom.bin` was 32819 bytes instead of 32768. `FLASH_Unlock` then printed "failed to load" even though the config loaded fine, and `FLASH_Lock` never shrank the file. **Fix:** any size loads, a warning is printed, and a save rewrites the file at exactly `EEPROM_SIZE`. | Fixed |
| I-6 | 4 | Medium | The Configurator and the joystick RC bridge can't run at the same time. Both default to TCP 5761, and SITL serves one MSP client per port. **Fix:** SITL's config default adds MSP on UART3, so the Configurator gets its own port on 5763. The launcher gains `-Configurator`, which starts `pnpm start` and checks that 5763 is up. An older `eeprom.bin` still needs deleting or the `serial 2 1 ...` CLI command. Verified on a fresh `eeprom.bin`: 5761/5762/5763 all bind, and with an RC client streaming on 5761, 5763 answers MSP and shows the live RC values. 5762 answers too. | Fixed |
| I-7 | 6a | Medium | On `wingflight_3d_2m`, `-Mode jsbsim` reported "roll and pitch respond in the WRONG direction" (roll −108°, pitch −36°). The signs were fine: JSBSim gives identical control directions for c172p and the 3D model. The check compared Euler angles after 1.5 s holds, and at ~400 °/s the roll wraps past 180° and the pitch goes over the top. Even with 300 ms holds, pitch was ambiguous because the plane was banked and diving. **Fix:** the extremes use a separate hold time (`-JsbsimHoldMs`, 300 ms for the 3D model, unchanged for the rest). The gate now also uses signed body rates from `MSP_RAW_IMU` (`-JsbsimRateThresholdDps`), which don't depend on attitude; their sign convention was checked against c172p. An axis passes on the rate or the attitude criterion, and a strongly negative rate still flags an inverted control. Side finding: `MSP_RAW_IMU` gyro values are raw LSB (16.4 per deg/s in SITL), not deg/s, because `gyroRateDps()` divides by the scale twice. | Fixed |
| I-5 | 4 | Low | "Reboot" ends the SITL process (`systemResetHard()` calls `exit(0)`). It does not restart, so SITL has to be started again by hand. This is by design for now. A second instance started while the old one still holds TCP 5761/5762 runs without MSP ports (seen here: PIDs 20204/41392). | Open |

## Automated results (step 2)

| Mode | Result |
|---|---|
| smoke | pitch servo 938 us |
| sweep | roll/pitch/yaw 938 us each |
| stress | 0 dropouts |
| jsbsim | roll +84.1°, pitch +41.7° signed, drift 1.1°/1.8° |
| throttle | MSP_MOTOR 1827 us, JSBSim thr 0.84, IAS +24.5 kt |
| gps | 10 sats, 173 m moved, 43 m/s, lat 37.8012 (see I-1) |

## 2. Configurator Start
- configurator start and connect successfully to the SITL
- if I activate a feature and hit saven and reboot, the SITL exists. Upon restart, the feature is not activated.
  - → I-3 (unsupported features are silently dropped, now logged), I-4 (`eeprom.bin` size), I-5 (reboot = exit).
    Reproduced over MSP (`MSP_SET_FEATURE_CONFIG` → `MSP_EEPROM_WRITE` → `MSP_SET_REBOOT` → restart):
    LED_STRIP is dropped with the new warning, and CMS persists.