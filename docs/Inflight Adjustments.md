# In-flight Adjustments

Wingflight lets you change settings while flying, using channels from your transmitter: a
switch to step a value up and down, or a knob to set it directly. You can also use the Wingflight
Lua scripts and the Configurator to set these up.

## Warning

Changing settings in flight can make the aircraft unstable and crash if you are not careful.

## Recommendations

* Always make adjustments in a large open area.
* Make small adjustments, and fly carefully to test each one.
* Give yourself space and time to see how a change affects the aircraft.
* Set adjustment switches and knobs to their centre position before powering on the transmitter and
  the aircraft.
* If you can, set switch warnings on your transmitter for dedicated adjustment switches.
* A momentary three-position switch, one that re-centres itself, is the best kind for stepping a
  value.

## How it works

Each adjustment is one of up to **42 slots**. A slot joins a **function** (the setting to change) to
the channels that control it:

* an optional **enable channel** and range: the adjustment is only live while that channel is inside
  the range. Use it to pick which function a shared switch or knob is currently controlling. The
  enable channel can be `255` for "always enabled".
* a **value channel**: the switch or knob that changes the value.
* two **value ranges**, and a **step size**, which decide how the value channel changes the value.

There are two modes, chosen by the step size.

### Stepped mode (step size not zero)

Use this with a three-position switch. While the value channel sits in the first ("decrease") range
the setting goes down by the step. While it sits in the second ("increase") range the setting goes
up by the step. The position must be held for 100 ms before the first step, and the step repeats
every 200 ms while the switch is held there (20 ms for servo trims, so they can be nudged quickly).
The value is kept between `value min` and `value max`.

### Continuous mode (step size zero)

Use this with a knob or slider. The position of the value channel inside the first range is mapped
straight onto the value range `value min` to `value max`. The second range is not used. The channel
must move more than 3 microseconds to change the value, so a noisy knob does not make the value
jitter.

A knob that is also used for another adjustment can make the second setting jump when it becomes
active, because the knob no longer matches the setting. Set knobs to match before enabling them.

## Configuration

Set adjustments up with the Configurator, or the CLI command `adjfunc`:

```
adjfunc <index> <func> <enable channel> <start> <end> <value channel> <dec start> <dec end> <inc start> <inc end> <step size> <value min> <value max>
```

| Argument | Meaning |
|---|---|
| `index` | Slot, 0 to 41 |
| `func` | Function number, from the table below |
| `enable channel` | AUX channel that enables the slot (AUX1 = 0, AUX2 = 1 and so on), or `255` for always |
| `start`, `end` | Range of the enable channel in which the slot is live, 900 to 2100 in steps of 25 |
| `value channel` | AUX channel of the switch or knob that changes the value |
| `dec start`, `dec end` | Range of the value channel that decreases the value (stepped), or that is mapped onto the value range (continuous) |
| `inc start`, `inc end` | Range of the value channel that increases the value (stepped). Unused in continuous mode |
| `step size` | Amount per step. `0` selects continuous mode |
| `value min`, `value max` | Limits on the value the slot may set |

The ranges must match the values your receiver sends. The enable channel and the value channel can
be the same. Run `adjfunc` with no arguments to show the slots.

## Servo trim

The `SERVO_TRIM_ROLL`, `SERVO_TRIM_PITCH` and `SERVO_TRIM_YAW` functions move the servo centres
of every servo driven by that axis, in microseconds (limited to plus or minus 200).

* In **stepped** mode the change edits the saved servo centre, and is saved with the rest of the
  configuration.
* In **continuous** mode the value is a **runtime-only** offset that follows the knob, is limited to
  20% of the servo travel, and is never saved. This is because a saved knob position would apply
  itself again on top of its own saved result after every reboot.
* Servo trims ignore the channels until the receiver link has been valid for a second, so the
  garbage frames some receivers send at power-up cannot move a control surface.

The `AUTO TRIM` mode (see [Modes](Modes.md)) is the other way to set the centres.

## Adjustment functions

Values are in the units of the matching setting. See [PID tuning](PID%20tuning.md) and
[Profiles](Profiles.md).

| ID | Function | Range |
|---|---|---|
| 0 | None | |
| 1 | `RATE_PROFILE` | 1 to 6 |
| 2 | `PID_PROFILE` | 1 to 6 |
| 3 | `LED_PROFILE` | 1 to 4 |
| 82 | `BATTERY_PROFILE` | 1 to 6 |
| 111 | `TV_PROFILE` | 1 to 6 |
| 5, 6, 7 | `PITCH_SRATE`, `ROLL_SRATE`, `YAW_SRATE` | 0 to 100 |
| 8, 9, 10 | `PITCH_RC_RATE`, `ROLL_RC_RATE`, `YAW_RC_RATE` | 1 to 200 |
| 11, 12, 13 | `PITCH_RC_EXPO`, `ROLL_RC_EXPO`, `YAW_RC_EXPO` | 0 to 100 |
| 68, 69, 70 | `PITCH_SP_BOOST_GAIN`, `ROLL_SP_BOOST_GAIN`, `YAW_SP_BOOST_GAIN` | 0 to 255 |
| 72, 73, 74 | `YAW_DYN_CEILING_GAIN`, `YAW_DYN_DEADBAND_GAIN`, `YAW_DYN_DEADBAND_FILTER` | 0 to 250 |
| 14, 15, 16, 17 | `PITCH_P_GAIN`, `_I_GAIN`, `_D_GAIN`, `_F_GAIN` | 0 to 1000 |
| 18, 19, 20, 21 | `ROLL_P_GAIN`, `_I_GAIN`, `_D_GAIN`, `_F_GAIN` | 0 to 1000 |
| 22, 23, 24, 25 | `YAW_P_GAIN`, `_I_GAIN`, `_D_GAIN`, `_F_GAIN` | 0 to 1000 |
| 56, 57, 58 | `PITCH_B_GAIN`, `ROLL_B_GAIN`, `YAW_B_GAIN` | 0 to 1000 |
| 84, 85, 86 | `MASTER_GAIN_PITCH`, `MASTER_GAIN_ROLL`, `MASTER_GAIN_YAW` | 25 to 1000 |
| 33, 34, 35 | `PITCH_GYRO_CUTOFF`, `ROLL_GYRO_CUTOFF`, `YAW_GYRO_CUTOFF` | 0 to 250 |
| 36, 37, 38 | `PITCH_DTERM_CUTOFF`, `ROLL_DTERM_CUTOFF`, `YAW_DTERM_CUTOFF` | 0 to 250 |
| 45 | `ANGLE_LEVEL_GAIN` | 0 to 200 |
| 46 | `HORIZON_LEVEL_GAIN` | 0 to 200 |
| 47 | `ACRO_TRAINER_GAIN` | 25 to 255 |
| 87 | `AUTOHOVER_GAIN` | 0 to 250 |
| 88 | `ATTHOLD_GAIN` | 0 to 250 |
| 64, 65 | `ACC_TRIM_PITCH`, `ACC_TRIM_ROLL` | -300 to 300 |
| 89, 90, 91 | `SERVO_TRIM_ROLL`, `SERVO_TRIM_PITCH`, `SERVO_TRIM_YAW` | -200 to 200 |
| 112 | `FLAP_COMPENSATION_GAIN` | 0 to 1000 |
| 113 | `DIFF_THRUST_YAW_GAIN` | 0 to 1000 |
| 92, 93, 94 | `TV_MASTER_GAIN_ROLL`, `_PITCH`, `_YAW` | 25 to 1000 |
| 95 to 99 | `TV_ROLL_P_GAIN`, `_I_GAIN`, `_D_GAIN`, `_F_GAIN`, `_B_GAIN` | 0 to 1000 |
| 100 to 104 | `TV_PITCH_P_GAIN`, `_I_GAIN`, `_D_GAIN`, `_F_GAIN`, `_B_GAIN` | 0 to 1000 |
| 105 to 109 | `TV_YAW_P_GAIN`, `_I_GAIN`, `_D_GAIN`, `_F_GAIN`, `_B_GAIN` | 0 to 1000 |
| 110 | `TV_HOLD_GAIN` | 0 to 250 |

`FLAP_COMPENSATION_GAIN` and `DIFF_THRUST_YAW_GAIN` change the weight of every mixer rule tagged
with the matching `role`. They set the size of the weight only: the polarity you set on the rule is
kept. See [Mixer](Mixer.md).

Other function numbers that appear in the firmware are unused leftovers from the helicopter code
and do nothing.
