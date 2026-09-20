# PID tuning

Wingflight's stabilised flight (the default "acro" mode, and the base under ANGLE, HORIZON and the
hold modes) is a **rate controller**. The pilot's stick sets a target rotation rate on each axis.
The controller compares it with the rate the gyro measures and drives the surface to close the
gap. The settings that shape it are stored per [profile](Profiles.md).

This page explains what each setting does and where to start. How each stage is built, and why, is
in [Flight Dynamics](FlightDynamics.md). Tune in flight and change one thing at a time. Use
blackbox logs (see [Blackbox](Blackbox.md)) to see what the controller did.

## The terms

Each axis (`roll_`, `pitch_`, `yaw_`) has these gains, for example `roll_p_gain`:

| Term | Setting | Job |
|---|---|---|
| **F** (feedforward) | `*_f_gain` | Drives the surface directly from the stick target. On a fixed wing this carries most of the stick response. The default F = 100 gives full travel at 400 deg/s |
| **P** | `*_p_gain` | Corrects the *error* between target rate and gyro. Keeps the aircraft tracking the stick and rejects disturbances. Too high and the surface oscillates |
| **I** | `*_i_gain` | Accumulates the error, so a steady disturbance (torque, trim change, a sagging setpoint) is cancelled. It has a limit (`error_limit`, degrees) and decays over time (below) |
| **D** | `*_d_gain` | Damps the rate of change of the gyro (not the stick). Defaults to 0. Adds noise and servo heat, so use it only if P alone cannot damp an oscillation |
| **B** | `*_b_gain` | Feedforward boost, from how fast the stick is moving. Defaults to 0 |

The gains are scaled differently per axis and per term. Yaw P and I are much larger than roll or
pitch at the same number, and roll D is ten times smaller than pitch D at the same number, so do
not copy gains between axes.

The output is a unitless surface command, where 1.0 is full travel of that mixer input. Servo end
points and rules are set in the [Mixer](Mixer.md).

## Where to start

1. **Set the rates first** (`*_rc_rate`, `*_srate`, `*_expo` in the rate profile), because F and
   the target rate work together.
2. **F**: with P, I and D low, fly and compare, in a blackbox log, the target rate with the gyro
   rate. If the gyro lags the target with a steady shortfall, raise F. If it overshoots, lower it.
3. **P**: raise until the aircraft tracks tightly, back off if the surface starts to oscillate or
   buzz, especially at high speed.
4. **I**: raise for a hold that resists a steady disturbance, such as torque roll in a hover.
   Lower it if you see slow wandering or bounce-back after a roll.
5. **D**: leave at 0 unless you need it.

## Scaling the whole loop

* `master_gain` (per axis, percent) scales P, I and D together. It can be changed in flight with an
  [adjustment](Inflight%20Adjustments.md).
* `gain_curve` (per axis) scales the master gain further with how far the stick is deflected.
  It uses one of the shared gain curves.
* `fw_tpa_gain` and `fw_tpa_curve` attenuate P and D as **throttle rises**. Throttle is a proxy
  for the prop-wash over the surfaces, not for airspeed. On a plane that hovers or hangs at high
  throttle, the surfaces are still very effective, so you want less gain, not more, as throttle
  goes up. I and F are not attenuated. The default has no effect.

## The I term

* `error_limit` (degrees, per axis) caps how much error the I term can accumulate.
* `iterm_relax_type`, `iterm_relax_level` and `iterm_relax_cutoff` reduce accumulation while the
  stick is moving fast, so a roll or flip does not wind I up.
* `iterm_decay_time` and `iterm_decay_limit` bleed I away over time. The default time constant is
  about 0.6 s. The bleed is slower or suspended in the self-levelling and hold modes, which need a
  sustained I term to hold an attitude.
* `cross_axis_relax_*` softens roll and pitch feedback while the rudder is being used, so a rudder
  input does not fight the stabilisation. Off by default.

## Filters

`*_gyro_cutoff` limits the bandwidth of the gyro signal used by the controller. `*_d_cutoff` and
`*_b_cutoff` filter the D and B terms. Filters add lag: the lower the cutoff, the more you lose
phase margin for P. Dynamic notch and RPM filters for prop and motor vibration are configured in
the gyro settings.

## Self-levelling and hold modes

These modes change the rate target the controller follows, and their gains are in the profile too:

| Mode | Settings |
|---|---|
| ANGLE | `angle_level_strength`, `angle_level_limit` |
| HORIZON | `horizon_level_strength`, `horizon_transition`, `horizon_tilt_effect` |
| ATT HOLD | `atthold_gain`, `atthold_deadband`, `atthold_max_rate` |
| AUTO HOVER | `autohover_gain`, `autohover_max_angle`, `autohover_max_rate`, `autohover_roll_deadband`, and the optional `autohover_throttle_assist_*` |
| TRAINER | `acro_trainer_gain`, `acro_trainer_angle_limit`, `acro_trainer_lookahead_ms` |

The gain settings are ten times the real gain, in degrees per second of correction per degree of
attitude error. `*_max_rate` caps the correction rate. See
[Flight Dynamics](FlightDynamics.md) for how each mode holds, and the tuning notes there.

## Thrust vectoring

If `feature THRUST_VECTOR` is on, the vectored-thrust outputs have their own independent PID loop
and profile, with `tv_` settings that mirror the ones above.
