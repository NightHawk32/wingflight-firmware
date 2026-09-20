# Mixer

Wingflight has a fully customisable, rule-based mixer. There are no built-in multirotor or
helicopter mixer presets: every servo and motor output is produced by a list of **rules** that
combine **inputs** (pilot commands, stabilised PID outputs, raw RC channels) into **outputs**
(servos `S1`..`S26`, motors `M1`..`M4`).

The default is a standard airplane with two ailerons, elevator, rudder and one motor. The
Configurator offers airframe templates (regular airplane, flying wing, V-tail, delta wing,
rudder/elevator trainer) that just fill in the rule table. The firmware does not branch on the
airframe type: `model_type` is descriptive only, so the Configurator knows whether to show its
simplified view or the raw rule editor.

For how the stabilised inputs are produced, see [Flight Dynamics](FlightDynamics.md).

## Inputs

Inputs are named in the CLI as follows.

| Name | Meaning |
|---|---|
| `SR` `SP` `SY` | Stabilised roll / pitch / yaw: the PID output (rate loop, plus any leveling or hold mode) |
| `ST` | Stabilised throttle: pilot throttle, plus AUTOHOVER assist, through the governor |
| `CR` `CP` `CY` `CT` | Pilot command roll / pitch / yaw / throttle, after deadband and scaling, before rates |
| `RR` `RP` `RY` `RT` | Raw RC channels 1-4 |
| `AUX1` `AUX2` `AUX3` `CH8`..`CH18` | Raw RC channels 5 and up |
| `TR` `TP` `TY` | Thrust-vector stabilised roll / pitch / yaw (only when `feature THRUST_VECTOR` is on) |

Each input also has a `rate` (multiplier) and `min`/`max` limits. When an input is clipped at
its limit, the PID's anti-windup sees it as saturated.

In **PASSTHROUGH** mode the stabilised roll/pitch/yaw inputs are replaced by the raw RC channels
(no rates, no PID). In **MANUAL** mode they are replaced by the pilot's rate-curve command with
no gyro correction. See [Modes](Modes.md).

## Rules

A rule is evaluated every PID loop:

```
value  = input * input.rate
value  = curve(value)              (optional)
weight = weight        if value >= 0
         weightNeg     if value <  0
out    = (offset + weight * value) / 1000
out    = slew-limited by `speed`   (optional)
```

then combined into the output with the rule's operation: `set` replaces the output, `add` adds to
it, `mul` multiplies it. Rules run in table order, so put `set` rules before the `add`/`mul` rules
that modify the same output. Up to 32 rules can be defined.

The CLI form is:

```
mixer rule <index> <oper> <input> <output> <weight> <offset> [weightNeg [speed [curve [condition [role]]]]]
```

* `weight` and `offset` are in thousandths (`1000` = 1.0). A negative weight reverses the output.
* `weightNeg` is used when the input is negative. Leave it out for a symmetric rule, or set it
  different from `weight` for differential ailerons or elevons.
* `speed` limits how fast this rule's contribution may change (0 = unlimited).
* `curve` is `0` for none, or `1`..`8` to run the value through that mixer curve first.
* `condition` is `0` for always active, or `1`.. to gate the rule on a
  [logic condition](Cli.md#logic-conditions) (`logic` command).
* `role` tags a rule so an [in-flight adjustment](Inflight%20Adjustments.md) can find it: flap
  compensation or differential-thrust yaw gain.

Other commands:

| Command | Purpose |
|---|---|
| `mixer` | Show all inputs, rules and curves |
| `mixer rule <index> del` | Delete a rule |
| `mixer rule reset` | Restore the default rules |
| `mixer input`, `mixer limit`, `mixer rate` | Show or set input options |
| `mixer curve` | Show or set mixer curves (up to 8 curves of up to 9 points) |
| `mixer status` | Show the live value of every output |
| `mixer reset` | Reset inputs, rules and curves |

### Default rules

| Rule | Output | Input | Weight |
|---|---|---|---|
| 0 | `S1` left aileron | `SR` | +1000 |
| 1 | `S2` right aileron | `SR` | -1000 |
| 2 | `S3` elevator | `SP` | +1000 |
| 3 | `S4` rudder | `SY` | +1000 |
| 4 | `M1` motor | `ST` | +1000 |

## Servos

Servo endpoints are set with the `servo` command, or in the Configurator's Servos tab:

```
servo <n> <mid> <min> <max> <rneg> <rpos> <rate> <speed> <flags>
```

* `mid` is the centre pulse in microseconds. `min` and `max` are the travel limits as offsets
  from `mid`. `rneg` and `rpos` scale a full-negative and full-positive command into microseconds.
* `rate` is the servo refresh rate in Hz. `speed` is a slew limit on the servo (0 = unlimited).
  Note that a non-zero `speed` on one servo also slows every other servo fed by roll or pitch,
  to keep them coordinated, which is not what you want for independent wing surfaces.
* `flags` includes the reverse bit.

After the mixer, each servo output goes through, in order: optional geometry correction, an
optional **balance curve** (a small corrective delta so two servos on one surface match across
the throw), the speed limit, reversal, `rneg`/`rpos` scaling, runtime trim, the travel limit,
then `mid`.

Servo centres can be trimmed in flight with the `SERVO_TRIM_*` adjustments, and `AUTO TRIM`
captures the current stabilised outputs as the new centre. See
[Inflight Adjustments](Inflight%20Adjustments.md).
