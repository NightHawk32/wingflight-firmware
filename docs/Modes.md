# Modes

Modes are switched on and off with receiver channels (switches or dials), and by some events
such as failsafe. The list below is the modes this firmware actually offers. Modes that existed in
Betaflight or Rotorflight but were removed (HEADFREE, AIR MODE, 3D, FLIP OVER AFTER CRASH,
ANTI GRAVITY, LAUNCH CONTROL, OSD DISABLE, AUTOROTATION, governor suspend/bypass and so on) do not
appear.

The **ID** is the permanent mode ID. It is what the `aux` CLI command uses, and it never changes
between firmware versions.

Which modes are offered depends on the hardware and features enabled: modes that need the
accelerometer appear only when one is present, GPS modes need `feature GPS`, and so on.

## Flight modes

Only one of AUTO HOVER, ATT HOLD, ANGLE, HORIZON and TRAINER is active at a time, in that order of
priority: if two are switched on, the earlier one in this list wins. GPS RESCUE, GPS LOITER, GPS RTH
and FAILSAFE take priority over all of them for the roll and pitch setpoint.

| ID | Name | Function | Needs |
|---|---|---|---|
| 0 | ARM | Enables motors and flight stabilisation | |
| 1 | ANGLE | Self-levelling: sticks command a bank/pitch angle, up to `angle_level_limit`, and centred sticks hold level. Yaw is left as a plain rate | ACC |
| 2 | HORIZON | Self-levelling around centre, blending to plain rate control as the sticks move out | ACC |
| 6 | ATT HOLD | Holds whatever attitude the aircraft was in when a stick returned to centre, in any orientation. Each axis tracks or holds on its own stick | ACC |
| 58 | AUTO HOVER | Holds a vertical (nose-up) attitude and heading for a prop-hang. Roll is the pirouette control | ACC |
| 47 | TRAINER | Limits the roll/pitch angle in acro | ACC |
| 59 | MANUAL | Rates and expo as normal, but **no gyro correction**: the pilot's rate command goes to the surfaces through the F gain | |
| 12 | PASSTHROUGH | Roll, pitch and yaw go straight from the receiver to the servos, with no rates and no stabilisation. Takes priority over MANUAL | |
| 65 | TRADITIONAL | Forces the I term output to zero on whatever stabilisation is active, so surfaces spring back on stick release | |
| 60 | AUTO TRIM | While on and armed, averages the stabilised servo outputs for 2 s and stores them as the new servo centres. Turn it off before disarming to cancel | |
| 55 | GOVERNOR | Engages the [governor](Governor.md), and is a motor interlock when a governor mode is configured | |
| 63 | THRUST VECTOR | Switches the independent thrust-vector PID loop on | `feature THRUST_VECTOR` |
| 64 | THRUST VECTOR ATTITUDE HOLD | Attitude/heading hold for the thrust-vector loop only, leaving the surfaces in rate mode | `feature THRUST_VECTOR` |

See [Flight Dynamics](FlightDynamics.md) for how each of these works and why.

### Navigation

| ID | Name | Function | Needs |
|---|---|---|---|
| 62 | GPS RTH | Experimental. Return to the launch point at `nav_rth_altitude`, banking to steer and pitching to hold altitude. Throttle stays manual. Unverified in flight | GPS |
| 61 | GPS LOITER | Experimental. Orbit the point where it was switched on. RTH wins if both are on. The direction setting appears inverted | GPS |
| 46 | GPS RESCUE | Inherited from Betaflight and tuned for multirotors. It does not steer or control altitude on a wing, so do not rely on it. Use GPS RTH | GPS |
| 37 | GPS BEEP SATELLITE COUNT | Beeps the number of satellites | GPS |

### Safety and arming

| ID | Name | Function |
|---|---|---|
| 36 | PREARM | When arming, wait for this switch before actually arming |
| 45 | PARALYZE | Permanently disable a crashed aircraft until it is power cycled |
| 27 | FAILSAFE | Treat the receiver as failed. See [Failsafe](Failsafe.md) |
| 51 | STICK COMMANDS DISABLE | Ignore stick-position commands such as arming and calibration |

### Peripherals and logging

| ID | Name | Function |
|---|---|---|
| 13 | BEEPER | Sound the buzzer, useful for finding a lost aircraft |
| 52 | BEEPER MUTE | Silence the buzzer |
| 15 | LEDLOW | Switch off the LED strip |
| 20 | TELEMETRY | Enable telemetry when it shares a port with something else |
| 26 | BLACKBOX | Start and stop blackbox logging |
| 31 | BLACKBOX ERASE | Erase the blackbox flash when the switch is turned on |
| 32, 33, 34 | CAMERA CONTROL 1-3 | Camera functions, for RunCam-protocol cameras |
| 39 | VTX PIT MODE | Switch the VTX to pit mode (low power) |
| 48 | VTX CONTROL DISABLE | Stop the flight controller controlling the VTX |
| 40, 41, 42, 43 | USER1-4 | Switches for driving a PINIO output |

## Auxiliary configuration

Spare receiver channels enable modes. Configure your transmitter so that switches or dials send
channels 5 and up (the first four are aileron, elevator, throttle and rudder), with values between
1000 and 2000. When a channel is inside a mode's range, that mode is on. The range is fixed to
900-2100 and set in steps of 25.

Use the Configurator's Modes tab, or the `aux` CLI command:

```
aux <slot> <mode id> <aux channel> <low> <high> [<logic> <linked mode id>]
```

* `slot`: 0 to 19.
* `mode id`: the permanent ID from the tables above.
* `aux channel`: AUX1 = 0, AUX2 = 1, and so on.
* `low`, `high`: the range, 900 to 2100. If they are equal the slot is ignored.
* `logic`: `0` (OR, the default) or `1` (AND). With AND, this range only counts when the mode it is
  linked to is also on, which lets one switch combine with another.

For example, to enable ARM when AUX1 is between 1700 and 2100:

```
aux 0 0 0 1700 2100
```

Running `aux` with no arguments shows the current configuration.
