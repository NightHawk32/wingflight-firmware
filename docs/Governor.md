# Governor

The governor sits between the pilot's throttle and the motor. It is a small, fixed-wing
oriented feature, and is **not** the Rotorflight helicopter governor (there are no
`gov_*` settings, no autorotation, no spool-up state machine and no headspeed curves).

It has three jobs, depending on the mode:

* hold a safe **idle** while the throttle stick is low (a powered idle, or a fixed throttle),
* hold a target **RPM** across the whole stick range, and
* **limit** the maximum RPM.

The governor is engaged with the `GOVERNOR` mode on a switch (see [Modes](Modes.md)). The
`GOVERNOR` switch is also a motor interlock; see [Interlock](#interlock).

## Settings

| Setting | Meaning |
|---|---|
| `governor_mode` | `OFF`, `RPM`, `THROTTLE` or `RPM_RANGE` |
| `governor_rpm` | Target RPM for the idle hold in `RPM` mode |
| `governor_gain` | Proportional gain (RPM mode and RPM_RANGE) |
| `governor_i_gain` | Integral gain |
| `governor_throttle` | Idle throttle (%). In `THROTTLE` mode this is the fixed output; in `RPM` mode it is the floor |
| `governor_handover` | Throttle stick position (%) below which the governor is in control; above it the stick has control again |
| `governor_ceiling` | Highest throttle (%) the governor may command |
| `governor_rpm_min`, `governor_rpm_max` | RPM at zero and at full stick in `RPM_RANGE`; also the RPM limit in `RPM` mode |

Defaults: mode `OFF`, gain 20, I gain 30, throttle 15%, handover 10%, ceiling 30%.

The RPM modes need an RPM source: DShot telemetry, an ESC telemetry sensor or a frequency sensor,
with `motor_poles` set correctly for motor 1.

**Note:** `governor_ceiling` defaults to 30%. That is a sensible idle cap, but in `RPM_RANGE` mode it
also caps the whole flight, so raise it (usually to 100) when you use that mode.

## Modes

### `OFF`

The governor does nothing; the throttle passes straight to the motor. If a `GOVERNOR` switch is
configured it is still an interlock (below).

### `THROTTLE`

No RPM feedback needed. While the stick is below `governor_handover` and armed with the
`GOVERNOR` switch on, the output is held at `governor_throttle` (never above `governor_ceiling`).
Above the handover point the stick controls the motor directly. Use it to keep a fixed idle on
ESCs that have no RPM telemetry.

### `RPM`

Below the handover point the governor holds `governor_rpm` using a P and I loop (the RPM error is
low-pass filtered at 2 Hz because a fixed-wing prop has very little inertia and an unfiltered
P term limit-cycles on RPM noise). The output never drops below the `governor_throttle` floor, so
the ESC stays alive through a rapid throttle chop. Above the handover point the stick takes over.

If `governor_rpm_max` is set, `RPM` mode also works as a limiter above the handover point: it
passes the pilot's throttle through unchanged and only pulls it down while the measured RPM is
above `governor_rpm_max`. The limiter can only remove throttle, never add it.

### `RPM_RANGE`

The whole stick range is governed: zero stick maps to `governor_rpm_min` and full stick to
`governor_rpm_max`, and the governor drives the throttle to hold that RPM. It uses a faster loop
than the idle modes so it can sweep the whole range (for example to hold a dive-speed target).
`governor_rpm_max` must be non-zero, and the output is never above `governor_ceiling`.

If RPM feedback is lost while governing, the governor drops to raw-stick pass-through at once
rather than freezing the output, and the `GOVERNOR` switch must be switched off and on again to
resume.

## Interlock

If `governor_mode` is not `OFF` **and** a switch is assigned to `GOVERNOR`, that switch acts as a
motor interlock: with the switch off the motor output is forced to zero and the stick has no
authority. This is deliberate, so a dedicated switch can be used for safe ground arming and
starting. If no switch is assigned to `GOVERNOR` there is no interlock, and the governor is simply
not engaged.

## Failsafe

When the receiver link is lost (or the `FAILSAFE` switch is on) the governor is bypassed entirely,
so the receiver-side failsafe throttle passes straight through. A `GOVERNOR` switch that is held
on through signal loss cannot keep the motor running. See [Failsafe](Failsafe.md).

## Output slew

The governor limits how fast its own output can change: 1.0 throttle-fractions per second in the
idle modes, and 5.0 in `RPM_RANGE` and in the RPM limiter. That prevents a step in motor output
when the switch is engaged or the handover point is crossed.

## AUTOHOVER throttle assist

The optional throttle assist in AUTOHOVER (see [Flight Dynamics](FlightDynamics.md)) is added
to the pilot's throttle before it reaches the governor, so the governor's slew and ceiling still
apply on top of it.
