# Wingflight Flight Dynamics: Design Rationale and Review

This document describes how Wingflight turns pilot sticks and sensor data into surface and motor commands. It records why each piece is built the way it is, and lists the problems found in a review of that code.

**Scope.** Everything between the receiver and the servo/motor outputs: setpoint shaping, the rate PID, the leveling and hold modes, the thrust-vector loop, the mixer, servo and motor output, the attitude estimator, and the arming/failsafe/navigation logic that overrides them.

**Basis.** A static read of `master`. Nothing was compiled, simulated, or flown for this review. The one thing checked numerically was the fast quaternion product in `imu.c`, which matches the Hamilton product to 1e-15. Where a finding depends on a sign convention or on behaviour I could not observe, it says so.

**Provenance of the rationale.** "Why" statements come from in-code comments and commit messages, and are quoted or paraphrased. Where neither exists, the text says *inferred*.

---

## 1. Conventions

These are easy to get wrong and are not written down elsewhere.

| Item | Convention | Evidence |
|---|---|---|
| Pitch sign | `attitude.values.pitch` and `attitude.raw[PITCH]` are **positive nose-down** (inherited Betaflight convention). Nose-up vertical is `-900` decidegrees. | [autohover.c](../src/main/flight/autohover.c) ("+900 drives the elevator toward nose-down, -900 is the physically-vertical, nose-up target"); [gps_rescue.c](../src/main/flight/gps_rescue.c) (positive angle = forward flight) |
| Yaw sign | RC yaw is clockwise-positive; gyro yaw is negative for the same motion. [setpoint.c](../src/main/flight/setpoint.c) negates RC yaw once, and everything downstream (setpoint, MANUAL, PASSTHROUGH) keeps that sign. | `setpointUpdate()`, `mixerGetPassthroughInput()` |
| Stabilized outputs | PID sum is a unitless surface command, ±1.0 = full mixer input. | `pidSum` into `MIXER_IN_STABILIZED_*`, limits ±1000 in [pg/mixer.c](../src/main/pg/mixer.c) |
| Axis error units | `axisError` is degrees (integrated rate error). `error_limit` is degrees. | [pid.c](../src/main/flight/pid.c) |
| Quaternion | `q` is sensor frame relative to earth frame. `imuEulerToQuaternion()` takes decidegrees in the `attitude.values` convention and negates yaw internally. | [imu.c](../src/main/flight/imu.c) |
| Loop timing | `pid.dT` comes from `gyro.targetLooptime`. With `pid_process_denom` > 1, [core.c](../src/main/fc/core.c) `taskMainPidLoop()` spreads setpoint → PID → mixer → servo across consecutive ticks, so output lags the PID by up to one denominator's worth of ticks. | `activePidLoopDenom` cases |

### Signal chain

```
 RX (rx.c) ─► rcInput ─► rc.c: deadband, ±1 deflection, throttle 0..1
                              │
                              ▼
        setpoint.c: airborne update ─► yaw sign ─► PT3 smoothing ─► yaw dynamic range
                    ─► response/accel limit ─► sp.deflection ─► FF boost ─► rates curve ─► sp.setpoint
                              │
                              ▼
        pid.c pidApplySetpoint(): first match wins
           ANGLE|GPS_RESCUE|FAILSAFE|LOITER|RTH → leveling.c angleModeApply
           AUTOHOVER → autohover.c        ATTHOLD → atthold.c → hold_engine.c
           HORIZON → horizonModeApply     TRAINER → acroTrainerApply     else: acro
                              │
                              ▼
        pid.c mode 1: P (TPA) + I + D + F + B  ─► pidSum   (mode 0: F only)
        tv_pid.c (optional, independent loop, reuses the final main setpoint)
                              │
                              ▼
        mixer.c: inputs → PASSTHROUGH / MANUAL override → rule table → outputs
                              │
                    servos.c (balance curve, speed limit, trim, travel)   motors.c (+ governor)
```

### Mode arbitration

[core.c](../src/main/fc/core.c) `processRxModes()` picks one leveling-family mode from the BOX switches. Priority: **AUTOHOVER > ATTHOLD > ANGLE > HORIZON > TRAINER**; the winner clears the others' flags, so with ANGLE and AUTOHOVER both switched on, AUTOHOVER runs and `ANGLE_MODE` is off.

Separately, the "safety" set (`ANGLE | GPS_RESCUE | FAILSAFE | LOITER | RTH`) preempts AUTOHOVER and ATTHOLD inside `pidApplySetpoint()`. GPS rescue, loiter and RTH are set from their own switches and are not cleared by the chain above, so they do preempt a hold. `core.c` then feeds the hold `mode && !safetyLevelingActive`, so the hold re-captures a fresh target when the safety mode releases.

`PASSTHROUGH`, `MANUAL` and `TRADITIONAL` are independent flags, not part of that chain.

---

## 2. Subsystems: what they do and why

### 2.1 Setpoint shaping — [setpoint.c](../src/main/flight/setpoint.c)

Order of operations: airborne update, yaw negation, PT3 smoothing (cutoff derived from measured RX frame interval), yaw dynamic deadband/ceiling, response-time and acceleration limit (→ `sp.deflection`), feed-forward "boost", rates curve (→ `sp.setpoint`).

- **Two outputs on purpose.** `sp.deflection` (before boost and curve) feeds the leveling/hold layers and MANUAL. `sp.setpoint` (after) feeds acro. The boost exists only to sharpen the gyro loop's target; it has no meaning for a mode that skips the gyro loop. Commit `062e79151`: reusing the boosted setpoint for MANUAL made fast stick moves saturate early "and felt like passthrough".
- **Smoothing tied to the RX frame rate.** Inherited from Rotorflight; it removes stair-stepping from 50–150 Hz link updates without a fixed lag.

### 2.2 Rate PID (mode 1) — [pid.c](../src/main/flight/pid.c)

`error = setpoint − filtered gyro`. Terms:

| Term | Formula | Notes |
|---|---|---|
| P | `Kp · masterGain · gainCurve · TPA · crossAxisRelax · error` | |
| I | `Ki · masterGain · crossAxisRelax · axisError` | Not attenuated by TPA. Forced to 0 output (state kept) under `TRADITIONAL_MODE`. |
| D | `Kd · masterGain · gainCurve · TPA · crossAxisRelax · d/dt(−gyro)` | Gyro-only D (no setpoint kick). Default D = 0. |
| F | `Kf · setpoint` | Default carries the whole stick response: F=100 → 0.0025/(°/s), so 400 °/s = full travel. |
| B | `Kb · d/dt(setpoint)` | FF boost; default 0. |

**Default authority budget** (from [pg/pid.c](../src/main/pg/pid.c) and [pid.h](../src/main/flight/pid.h) scale constants):

| Axis | P at 100 °/s error | I ceiling (Ki · `error_limit`) |
|---|---|---|
| Roll / pitch | 0.033 | 0.0032 × 45° = 0.144 |
| Yaw | 0.53 | 0.010 × 60° = **0.60** |

Yaw I can therefore drive 60% of travel on its own. That is deliberate room for rudder trim under prop torque, but it is large enough to matter for the anti-windup review below.

**Design decisions:**

- **Throttle-based gain attenuation (`fw_tpa_gain`, `fw_tpa_curve`).** Throttle is used as a proxy for prop-wash over the surfaces, "not airspeed": on aircraft that hover or harrier at or past stall, surfaces stay authoritative at high throttle regardless of airspeed, so gain falls as throttle *rises*. Structured like `master_gain` + `gain_curve` (baseline scale × optional curve from the shared pool) so tuners have one mental model. Applies to P and D only.
- **Master gain applied at the point of use**, not baked into `coef[]`, so any live adjustment stays correct regardless of which adjustment last touched a coefficient.
- **I-term error relax** (`iterm_relax_*`): high-pass of the setpoint scales accumulation down during fast stick motion. Inherited.
- **Cross-axis relax** (`cross_axis_relax_*`): yaw activity softens roll and/or pitch feedback "so rudder does not feel like an artificial hold". Default off.
- **Anti-windup:** accumulation stops only when the mixer input for that axis is saturated *and* the error would push further into saturation (`pidAxisSaturated`). Servo travel clipping also raises saturation (`mixerSaturateServoOutput`), so trim-induced clipping is covered.
- **`rotateAxisError()`:** rotates roll/pitch `axisError` by the yaw gyro so a stored error stays fixed in the earth-ish frame during a yaw rotation. Physically correct for a knife-edge or hover, harmless in cruise.
- **I-term decay policy** (the most-edited rule in this file):
  - Default `iterm_decay_time=6` → decay rate 10/6 s⁻¹, i.e. τ ≈ 0.6 s, capped at 35 °/s. Plain acro/manual flight bleeds I at this rate.
  - **Suspended** on roll/pitch while any of ANGLE/HORIZON/GPS-rescue/failsafe/loiter/RTH/TRAINER shapes the setpoint, and on any AUTOHOVER axis that is holding. Reason (commit `8a2b5233c`): the decay was inherited from the helicopter lineage to stop servo creep, but it "quietly erod[es] exactly the sustained I-term those layers need … felt like the correction giving up after a couple of seconds".
  - **Slowed to 10%** (τ ≈ 6 s, 3.5 °/s) on an ATTHOLD axis that is holding (`QUATHOLD_HOLD_I_DECAY_SCALE`), except for 3 s after a stall re-capture, when it runs at full rate (see §2.5). Reason (commit `10bf9b81d`): with no bleed, stale I "parks the surfaces off-centre forever" at zero error and zero motion, e.g. on the bench. A real steady disturbance is still held because the outer loop re-grows the I it needs.
  - **Unchanged (full rate)** for an axis that is free-tracking or settling inside a hold mode. Commit `92c9de190`: it is plain rate flight, so it should not carry stale I from an earlier manoeuvre into the next hold.
- **Gyro overflow** (`gyroOverflowDetected()`): `pidReset()` zeros all PID state and outputs until the gyro has read sane values for 50 ms. Protects against "yaw spin to the moon" after a crash-level over-range.

**Mode 0** (`pid_mode` ≠ 1) outputs F only: a stick-proportional command with no stabilization.

### 2.3 PASSTHROUGH and MANUAL — [mixer.c](../src/main/flight/mixer.c), [setpoint.c](../src/main/flight/setpoint.c)

Two distinct modes since commit `621dd3712`:

- **PASSTHROUGH** replaces the stabilized mixer inputs with raw RC channels. No rates, no expo, no PID. Takes priority over MANUAL. TV stabilized inputs are zeroed because no raw channel maps to them. This is the "bail-out" mode.
- **MANUAL** keeps the pilot's rates/expo but drops the gyro correction: output = `Kf · applyRatesCurve(sp.deflection)`, clamped ±1. Rationale (commit `a40c7860a`): MANUAL is meant to be "stabilized flight minus the gyro correction", and F is exactly that contribution, so reusing it makes MANUAL track however the airframe's F was tuned instead of an arbitrary fixed ceiling. Two earlier attempts (`062e79151`, `ec909933a`) had made MANUAL either saturate early or ignore the configured rates.

See finding **M-3** for the consequence of tying MANUAL's authority to F.

### 2.4 ANGLE and HORIZON — [leveling.c](../src/main/flight/leveling.c)

Standard Betaflight Euler-angle leveling on roll and pitch only; yaw stays a pass-through so the rudder is never held. Target angle = stick × `level_limit` (default 55°) + GPS-rescue/nav offsets, clamped to the limit. Rate command = error × `level_strength/10` (default 4 °/s per °). HORIZON blends that into the acro setpoint by stick position and inclination. Below "airborne" the error is scaled to 25% (see **H-4**).

Euler math is acceptable here because ANGLE is limited to 55° and pitch never nears ±90°. This is exactly why AUTOHOVER and ATTHOLD do *not* use it.

### 2.5 Attitude hold engine — [hold_engine.c](../src/main/flight/hold_engine.c), [atthold.c](../src/main/flight/atthold.c), [tv_hold.c](../src/main/flight/tv_hold.c)

**What it does.** Per axis, the hold either *tracks* (pilot stick outside `deadband`: setpoint passes through and the target follows the aircraft) or *holds* (stick centred and the axis has stopped rotating: rate command = attitude error × gain, clamped to `max_rate`).

**Key decisions:**

- **Quaternion error, not Euler.** Needed to work through inverted, knife-edge and harrier attitudes where Euler roll/pitch couple. Error is `2 × vector part` of `q_current⁻¹ · q_target` (shortest-path sign-corrected). That is singularity-free across 0–180°, at the cost of a non-linear measure: it saturates at 2 rad (≈115°) and reads 81° for a true 90° error, so gain is effectively lower at large errors.
- **Per-axis independence** (commit `4d714a433`). A blackbox from a real flight showed roll never correcting torque roll while the pilot held elevator: the earlier gate was one OR across all three sticks. Each axis now tracks or freezes on its own stick. The target is advanced by zeroing the tracking axes' components of the error quaternion and recomposing, never by extracting Euler angles.
- **Settle-then-capture** (`HOLD_SETTLE_RATE` 15 °/s or `HOLD_SETTLE_MAX_S` 0.4 s). Freezing at the instant the stick centres pins the target to an attitude the aircraft is still rotating through, and the hold hauls it back: a rubber-band snap-back "unlike plain acro, where the rate loop just brakes and the attitude stays put". The time cap stops a persistent disturbance rotation from keeping an axis in tracking forever.
- **Stall timeout** (error > 5° and rate < 5 °/s for 3 s → re-capture). A hold pinned against something it cannot move (aircraft on the bench, surface with no authority) gives up instead of leaving surfaces pegged. A small sag held by I stays under the threshold. This is silent to the pilot. **Wound-up I is bled at the full rate for 3 s after a re-capture** (`HOLD_STALL_BLEED_S`, `quatHoldIDecayScale()`). Without that, the 10% hold-rate bleed took about 15 s to re-centre roll/pitch (longer on yaw, whose I ceiling is 0.60 of travel): 3 s stall timeout, then about 7 s of linear decay at 3.5 °/s, then an exponential tail. Now it re-centres in roughly 5–6 s in total, with no step, because it decays rather than resetting.
- **Pre-airborne authority reduced, not zeroed** (commit `8a2b5233c`). Forcing passthrough on the ground made both hold modes look dead on the bench. They now use the same 25% scaling that ANGLE/HORIZON already used.
- **One engine, two instances** (commits `50b444423`, `83a5965e0`). ATTHOLD and the thrust-vector hold each own a `quatHold_t` but run identical code, so a fix lands in both.
- **Clamps at load, not only at the CLI** (commit `fa54e333d`). MSP `SET_PID_PROFILE` writes the raw byte with no clamping, so `deadband > 100` would have made the tracking test never trip and frozen the hold at full stick.
- **Re-capture after a safety mode** (commit `fa54e333d`). The AUTOHOVER/ATTHOLD flag stayed set while a safety mode preempted the setpoint, so the hold never saw a rising edge and resumed a stale target. `core.c` now feeds `mode && !safetyLevelingActive`.

**Known limitation (documented in source):** does not subtract `accelerometerTrims`, unlike `leveling.c`/`trainer.c`, so a pilot with board-mount trim dialled in will hold slightly off where the sticks were released.

### 2.6 AUTOHOVER — [autohover.c](../src/main/flight/autohover.c)

Quaternion vertical (nose-up) attitude and heading hold for 3D prop-hang.

- **Target** = vertical at the captured heading, plus pitch/yaw stick deflection (× `max_angle`, default 30°) as a small body-frame offset. Right-multiplied so stick feel does not depend on which way the held heading points.
- **Roll is the pilot's pirouette axis** (roll coincides with world-vertical at hover). Free while the stick is deflected. Since commit `7bc87e81b` it is captured and held once the stick centres, so torque roll no longer goes uncorrected.
- **Roll-hold only runs near vertical** (pitch+yaw error < 30°). Far from vertical the quaternion error does not decompose per axis, so `errorDeg[0]` picks up pitch/yaw error and feeds the roll-offset integrator: a bench-confirmed runaway (commit `8a2b5233c`).
- **Two-stage roll lock** (commit `51ba254f0`). Tracking starts at 30° but freeze and the settle countdown only run inside 10°. Before this, the settle clock's 0.4 s cap expired mid-flare while torque roll still spun the aircraft, freezing a target the aircraft was still rotating through. Leaving the 30° band now clears the capture.
- **Default `max_rate` 300 → 120 °/s** (same commit): engaging in forward flight now makes a wide turn instead of a hard 90° snap. At gain 5 that saturates at 24° of error.
- **Optional throttle assist** (`throttle_assist_*`, gain 0 = disabled by default; commit `befd8f6cf`). Ramps a bounded throttle add when pitch correction stays pinned at `max_rate`, as a proxy for "the airframe cannot out-thrust the hold". Ramped both ways, capped by a 50% firmware backstop independent of CLI/MSP values. See finding **M-1**.
- **Pitch sign**: `imuEulerToQuaternion(roll, −900, heading)`. Commit `89f531016` fixed an initial nose-down target.

Limitations already in the source comment: no `accelerometerTrims`; heading captured from an `atan2` that degrades toward 90° pitch; attitude only, no position hold; manual throttle.

### 2.7 Acro trainer — [trainer.c](../src/main/flight/trainer.c)

Angle-limits roll/pitch in acro using a projected-angle lookahead. Stateless (no latch). Inherited unchanged.

### 2.8 Thrust-vector loop — [tv_pid.c](../src/main/flight/tv_pid.c), [tv_hold.c](../src/main/flight/tv_hold.c)

A second, independent rate PID for vectored-thrust actuators (commit `107cfee43`), feeding `MIXER_IN_STABILIZED_TV_*`. It runs after the main PID and reuses the main loop's final setpoint, so it inherits any leveling/hold shaping. `BOXTHRUSTVECTOR` gates it live; while off it is reset so re-engaging ramps from zero instead of bumping. The optional TV hold (commit `83a99b1ba`) lets a jet hold attitude on the nozzle alone while the surfaces stay in acro, useful post-stall. It yields to the safety set so a stale target never fights a recovery.

It deliberately has no TPA, cross-axis relax, `TRADITIONAL_MODE`, or `rotateAxisError`.

### 2.9 Mixer — [mixer.c](../src/main/flight/mixer.c)

A rule table: `output (SET/ADD/MUL) = offset + weight · curve(input · rate)`, with a separate negative weight, an optional slew (`speed`), and an optional logic-condition gate. Default wing layout (S1/S2 ailerons from stabilized roll, S3 elevator, S4 rudder, M1 throttle) is in [pg/mixer.c](../src/main/pg/mixer.c).

- **Live-adjustable rule weights** (flap compensation, differential-thrust yaw) find rules by *role tag*, because nothing reserves fixed rule slots and the configurator reorders freely. The adjustment writes only |weight|; the pilot's polarity is kept in a separate `mixerRuleSign[]` because a live magnitude passes through 0, which has no sign.
- **Saturation flags** last `MIXER_SATURATION_TIME` (5) ticks and gate the PID's anti-windup.
- **Overrides and wiggle** apply only while disarmed.

### 2.10 Servo output — [servos.c](../src/main/flight/servos.c), [autotrim.c](../src/main/flight/autotrim.c)

Order: mixer output → geometry correction → **balance curve** → speed limit → reverse → `rpos`/`rneg` scale → runtime trim → travel limit → `mid`.

- **Balance curve** (commit `aa362d09a`): a small additive delta so two servos on one surface can be matched across the throw, the same idea as ETHOS "Balance channels". Additive, not a reshape, so an unconfigured curve is a zero delta.
- **Trim is split in two** (commit `220980d8d`). Switch-stepped and auto trim edit `mid` (saved). A continuous pot/channel trim is a **runtime-only** offset limited to 20% of scale and never saved. If it were saved, the pot would apply itself on top of its own saved result after every reboot, and a bad reading at boot would leave a wrong centre behind. Trim direction folds together three independent reversals (axis input rate sign, rule weight sign, servo REVERSED flag) so paired surfaces stay coordinated.
- **Auto trim** (from iNav's BOXAUTOTRIM): while the switch is on and armed, averages each stabilized servo's final output for 2 s and stores it as the new centre. Turning the switch off before disarm reverts it. It removes the runtime trim from what it stores.

### 2.11 Throttle, governor, motors — [governor.c](../src/main/flight/governor.c), [motors.c](../src/main/flight/motors.c)

Throttle goes `getThrottle()` → AUTOHOVER assist → `governorApply()` → mixer M1 → `motorUpdate()`. The governor has four modes (off, RPM idle-hold, fixed throttle, RPM range) plus an RPM max limiter. Notable decisions:

- When a governor mode is configured **and** a switch is assigned to `BOXGOVERNOR`, that switch is a **hard motor interlock**: stick has no authority until it is engaged. The `isModeActivationConditionPresent()` guard stops a configured-but-unwired governor from hard-cutting the motor forever.
- **RX loss bypasses the governor** so a held-on governor switch cannot override the failsafe throttle cut. It tests `!rxIsReceivingSignal()` first because `failsafeIsActive()` is unreliable while monitoring is disabled (see **H-1**).
- The P term is low-passed at 2 Hz for idle modes because a fixed-wing prop has almost no inertia and an unfiltered P limit-cycles on RPM quantisation.

### 2.12 Attitude estimator — [imu.c](../src/main/flight/imu.c)

Mahony quaternion filter (gyro integration corrected by accelerometer, plus magnetometer or GPS course when available).

- Accelerometer accepted only within 0.9–1.1 g.
- Gain is ×10 while disarmed, and after a disarm a 250 ms gyro-quiet period triggers a 500 ms high-gain re-convergence, to recover from a crash-induced bad attitude.
- Integral feedback stops above 20 °/s spin.
- Heading: magnetometer, else GPS course over ground above 5 m/s with ≥5 satellites.

### 2.13 Airborne detection — [airborne.c](../src/main/flight/airborne.c)

State machine: LANDED → AIRBORNE when armed and any of {peak stick > 2.5% (`rc_threshold`/1000), tilt cosine < 0.8 (≈37°), failsafe/GPS-rescue}. Back to LANDED when disarmed, or when sticks < 1.7% *and* tilt cosine > 0.9 (≈26°) *and* no failsafe. Consumers: the 25% authority scaling in leveling and the hold modes, the AUTOHOVER assist gate, SmartFuel, blackbox. Inherited from the helicopter lineage; see **H-4**.

### 2.14 Arming, failsafe, navigation

- **Arming.** `isUpright()` checks only that the attitude estimate is established, not that the aircraft is level (see **L-4**). That suits hand-launched wings, but the name is misleading. A re-arm grace window after an in-flight disarm relaxes the throttle and attitude checks.
- **Failsafe.** See **H-1**. Effective behaviour today is receiver-side substitution in [rx.c](../src/main/rx/rx.c) only.
- **RTH and Loiter** ([gps_nav.c](../src/main/flight/gps_nav.c), commit `255829c23`): produce roll and pitch *angle* targets that flow through the ANGLE-mode path. Track error → bank (P only, default 2.0 °/°, max 25°); altitude error → pitch (default 1 °/m, max 15°). No throttle, no wind compensation.
- **GPS Rescue** ([gps_rescue.c](../src/main/flight/gps_rescue.c)) is Betaflight's quad code. See **H-2**.

---

## 3. Decision log

Rationale reconstructed from commit messages. Dates are commit dates.

| Date | Commit | Decision | Why |
|---|---|---|---|
| 2026-06-28 | `d60575985` | Fork Rotorflight into a fixed-wing firmware | Wing-first behaviour; heli slots kept as reserved IDs for MSP/BOX compatibility |
| 2026-07-05 | `e10a57cd4`, `16b0e68d9` | Remove Euler ATTHOLD; add quaternion AUTOHOVER | Euler math gimbal-locks at 90° pitch, the one attitude hover lives at |
| 2026-07-05 | `621dd3712` | Split MANUAL from PASSTHROUGH | Separate a "no gyro but rates/expo" mode from a raw bail-out |
| 2026-07-08 | `b9d03d122` | Allow aileron control in AUTOHOVER | Roll is world-vertical at hover, so it is the pirouette control |
| 2026-07-09 | `3799be71d` | Re-add ATTHOLD on quaternion math | Works through any attitude |
| 2026-08-09 | `107cfee43` | Independent thrust-vector PID loop | Vectored nozzles need tuning separate from surfaces |
| 2026-08-29 | `83a99b1ba` | Independent TV attitude hold | Hold heading on the nozzle while surfaces stay in acro (low airspeed, post-stall) |
| 2026-09-11 | `062e79151`, `ec909933a` | MANUAL built from `sp.deflection` and the rate curve | Boost saturated early; normalizing by own rcRates cancelled the rates |
| 2026-09-17 | `4d714a433` | Hold each ATTHOLD axis independently | Real-flight blackbox: roll was uncorrected while elevator was held |
| 2026-09-17 | `7bc87e81b` | Hold AUTOHOVER roll once the stick centres | Torque roll went uncorrected |
| 2026-09-17 | `8a2b5233c` | 25% pre-airborne authority; suspend I decay under leveling; near-vertical gate on roll hold | Bench: modes looked dead, correction "gave up", roll-hold runaway |
| 2026-09-17 | `a40c7860a` | MANUAL scaled through F | Track the airframe's real tuning |
| 2026-09-18 | `92c9de190`, `05bc541c5` | Settle-then-capture for ATTHOLD and AUTOHOVER roll | Remove release snap-back |
| 2026-09-18 | `10bf9b81d` | 10% I decay and stall timeout on holds | Re-centre surfaces when nothing is happening |
| 2026-09-18 | `50b444423`, `83a5965e0` | Extract shared hold engine | One fix, both loops |
| 2026-09-18 | `befd8f6cf`, `fa54e333d` | Optional AUTOHOVER throttle assist; fix stale targets under safety modes, unclamped deadbands, zero-max-rate trigger | Under-thrust hovers; MSP has no clamping |
| 2026-09-18 | `aa362d09a` | Per-servo balance curve | Twin servos on one surface can bind |
| 2026-09-19 | `220980d8d` | Continuous servo trim is runtime-only | Saved pot trim re-applied itself every boot |
| 2026-09-19 | `51ba254f0` | Two-stage roll lock; `max_rate` 300 → 120 | Snap-back mid-flare; hard 90° snap on engage |

---

## 4. Review findings

Severity reflects consequence in flight, not effort to fix. **Confirmed** = follows directly from the code as read. **Needs verification** = depends on a convention or runtime behaviour I could not observe.

### High

**H-1. The flight-controller failsafe state machine never runs.** *Confirmed.*
[failsafe.c:118](../src/main/flight/failsafe.c#L118) `failsafeStartMonitoring()` has its body commented out ("RTFL: Keep disabled until code refactored"), so `failsafeIsMonitoring()` is always false and [failsafe.c:204](../src/main/flight/failsafe.c#L204) makes `failsafeUpdateState()` return immediately. Consequences:
- `FAILSAFE_MODE` is never set from link loss. No landing, no drop/disarm, and `failsafe_procedure`, `failsafe_delay`, `failsafe_off_delay` and `failsafe_throttle_low_delay` have no effect on link loss.
- `GPS_RESCUE` cannot be entered by failsafe.
- What does happen is [rx.c](../src/main/rx/rx.c) `detectAndApplySignalLossBehaviour()`: hold last value 300 ms, then per-channel fallback (default AUTO: roll/pitch/yaw centred, throttle just below the off-throttle threshold, **all other channels hold last value**, including the arm switch and any mode switches).
- [Failsafe.md](Failsafe.md) used to be Betaflight's text and described the disabled stage-2 behaviour as if it worked. It has been rewritten to describe what actually happens. The governor code already knew about the stub; the CLI settings still exist and do nothing.

*Fix.* Either enable and adapt stage 2 for wings (what should a plane do: level and cut, or RTH?), or delete the dead settings. Until then, tell pilots to configure the receiver's own failsafe, including the mode switches.

**H-2. GPS Rescue does not steer or control altitude on a wing.** *Confirmed.*
`gpsRescueGetYawRate()` and `gpsRescueGetThrottle()` ([gps_rescue.c:653](../src/main/flight/gps_rescue.c#L653), [:658](../src/main/flight/gps_rescue.c#L658)) are declared but called from nowhere, and `gpsRescueAngle[AI_ROLL]` is only ever set to 0. What remains active is the pitch angle from Betaflight's *quad* speed controller: below target ground speed it commands more nose-down pitch. In `GPS_RESCUE_MODE` a wing therefore levels the wings and pitches down with no heading correction and no thrust control. Only reachable via `BOXGPSRESCUE` today (see H-1), but it is a selectable mode.

*Fix.* Hide/disable it for wings and point pilots at RTH, or reimplement on the RTH path.

**H-3. RTH altitude hold is probably inverted.** *Needs verification (bench or SITL).*
[gps_nav.c:139](../src/main/flight/gps_nav.c#L139): `pitchDdeg = Kp · (target − current)`; positive means "aircraft is below target". `navAngle[PITCH]` is added to a target angle in [leveling.c](../src/main/flight/leveling.c) where pitch is **positive nose-down** (§1). So being below target commands nose-down, which descends further. Two independent code comments (AUTOHOVER `-900` = nose-up; GPS rescue positive = forward flight) support that convention. Roll sign checks out (target clockwise of course → positive → right bank).

*Fix.* Negate the pitch term once confirmed. Add a unit test for both signs.

**H-4. "Airborne" is read from stick and tilt only, so a level, hands-off aircraft counts as landed.** *Confirmed by code; effect size needs flight data.*
[airborne.c:103](../src/main/flight/airborne.c#L103) `touchdown()` is true when armed, sticks < ~1.7%, tilt cosine > 0.9, no failsafe. A wing cruising level with hands off satisfies that within a few seconds of stick release (the stick peak filter decays with a 0.5 Hz cutoff). `isAirborne()` then false scales the error to **25%** in ANGLE/HORIZON ([leveling.c:215, :230](../src/main/flight/leveling.c#L215)), ATTHOLD/TV hold ([hold_engine.c:210](../src/main/flight/hold_engine.c#L210)) and AUTOHOVER ([autohover.c:341](../src/main/flight/autohover.c#L341)). Leaving LANDED needs tilt beyond 37° (or stick input) while re-entering it needs tilt under 26°, so a hands-off aircraft that is already LANDED and gets disturbed to 30° bank keeps the weak gain until 37°. The most common use of a hold mode (release the sticks, expect it to hold) is exactly the case where the hold is quartered. The same applies to a hand launch with centred sticks.

The 25% rule was added so modes can be tested on the bench without snapping (commit `8a2b5233c`); the airborne signal was designed for a helicopter and has no throttle or airspeed input here.

*Fix options.* Include throttle above idle (or motor output) in liftoff and require it below idle to land; or scale by a separate "on the ground" test (no throttle and low gyro), not by stick activity.

### Medium

**M-1. AUTOHOVER throttle assist ignores the throttle stick and RX loss.** *Confirmed.*
[mixer.c:341](../src/main/flight/mixer.c#L341) adds `autoHoverThrottleBoost()` to `getThrottle()` unconditionally, up to `throttle_assist_max` (default 15%, hard cap 50%). With the stick at idle the motor can spin up, and on link loss the held-on AUTOHOVER switch plus centred sticks can keep assisting (H-1 means no failsafe mode clears it). The governor has an explicit failsafe bypass for the same reason; this path does not. Disabled by default (`throttle_assist_gain=0`), so exposure is limited to users who enabled it.

*Fix.* Gate the boost on the throttle stick being above off-throttle and on `rxIsReceivingSignal()`.

**M-2. Loiter direction is inverted.** *Confirmed by geometry.*
[gps_nav.c:125](../src/main/flight/gps_nav.c#L125): clockwise adds +90° to the *bearing to the target*. Aircraft south of the target (bearing 0°) is sent east (90°), which is counter-clockwise. `nav_loiter_direction = CW` orbits CCW and vice versa. Also: the orbit has no radial correction, so it circles at whatever radius it entered, and it steers on GPS course over ground, which is undefined at low groundspeed.

**M-3. MANUAL authority is coupled to F and to the rate profile.** *Confirmed.*
[setpoint.c:182](../src/main/flight/setpoint.c#L182): output = `Kf · rate`. At default F=100 a 400 °/s rate is full travel; at `F=0` MANUAL outputs **nothing**, and a milder rate profile gives proportionally less throw. That was intended (commit `a40c7860a`), but MANUAL is the fallback a pilot reaches for when the stabilized loop is misbehaving, and a tuning choice made for the PID (lower F, lower rates) silently shrinks it.

*Fix.* Give MANUAL its own scale or a floor, or warn at configuration time when `F × max rate < 1`.

**M-4. No guard against NaN/Inf at the servo boundary.** *Confirmed (defence in depth).*
[servos.c:370](../src/main/flight/servos.c#L370) writes `lrintf(pos · resolution)` straight to the timer compare register. `limitTravel` uses `>`/`<`, which are false for NaN, so a NaN from any upstream source would pass through as an undefined integer. I found no unguarded divide today ([rc.c:235](../src/main/fc/rc.c#L235) `range = deflection − deadband` would divide by zero only if the config allowed deadband ≥ deflection). One `isfinite` check in `servoUpdate()` and in `mixerUpdate()` closes it.

**M-5. Attitude estimator assumes the accelerometer reads gravity.** *Needs flight data.*
[imu.c:320](../src/main/flight/imu.c#L320) accepts 0.9–1.1 g. For a wing, sustained banked turns below ~25° bank and thrust acceleration both stay inside that window and bias the tilt estimate toward the turn or the acceleration. AUTOHOVER and every leveling mode depend on that estimate. There is no centripetal or airspeed correction. Kp and Ki defaults were not reviewed here. Worth checking against a blackbox of a sustained turn before tuning hold gains.

**M-6. Servo `speed` limiting couples every roll/pitch surface.** *Confirmed; default off.*
[servos.c:424–455](../src/main/flight/servos.c#L424): a slow servo's overrun scales *all* servos fed by roll or pitch (`cyclic_ratio`), so one rate-limited flap or aileron slows the elevator too. That is right for a helicopter swashplate and wrong for independent wing surfaces. `DEFAULT_SERVO_SPEED` is 0, so it only bites when a pilot sets a speed.

### Low

- **L-1. Heli remnants in the adjustment code.** [pid.c:253, :308, :319](../src/main/flight/pid.c#L253) scale roll D, pitch B and roll B by `pidMode == 4`. Mode 4 does not exist here, so those branches are dead.
- **L-2. Roll D scale is 10× smaller than pitch and yaw.** [pid.h:40](../src/main/flight/pid.h#L40) `ROLL_D_TERM_SCALE 0.1e-6` vs `1.0e-6`. Inherited. Confirm it is intended for wings. It is also mirrored in the TV loop.
- **L-3. Cross-axis relax is applied twice to I.** [pid.c:850, :863](../src/main/flight/pid.c#L850): it scales the accumulation *and* the I output, so I output drops immediately on rudder input and jumps back on release (limited only by the relax filter's 1–100 Hz cutoff). Default strength is 0, so latent.
- **L-4. `isUpright()` does not check attitude.** [imu.c:628](../src/main/flight/imu.c#L628) returns "attitude established". Arming is therefore not blocked by tilt. Probably right for wings; rename or comment it.
- **L-5. GPS heading re-initialisation is a no-op.** [imu.c:481](../src/main/flight/imu.c#L481) writes into the quaternion *products* `qP`, then `imuComputeRotationMatrix()` recomputes `qP` from the unchanged `q`. Only `attitudeIsEstablished` and the one-shot flag change. Harmless in effect, but the code does not do what its comment says.
- **L-6. Angle/horizon rate command is uncapped.** [leveling.c:218](../src/main/flight/leveling.c#L218): `error × gain` up to 180° × 4 = 720 °/s on a recovery from inverted. Surfaces saturate first, so it is safe, but it exceeds the configured rate profile.
- **L-7. MANUAL and PASSTHROUGH bypass mixer input limits** ([mixer.c:309–330](../src/main/flight/mixer.c#L309)). Intentional for bail-out; note that a tightened `min/max` on a stabilized input does not apply in those modes.
- **L-8. TV loop I-decay ignores leveling modes.** The main loop suspends decay under ANGLE/HORIZON/RTH; the TV loop only slows it under its own hold. In ANGLE mode the two loops therefore bleed I differently.
- **L-9. Hold stall re-capture is silent.** After 3 s pinned above 5° error the target ratchets to the current attitude with no beep, OSD or log flag. A genuine slow disturbance (cross-wind hover) could be walked off target this way without the pilot knowing.
- **L-10. Auto trim captures whatever the sticks and stabilization are doing** during its 2 s window, not a true neutral. It needs hands-off, straight-and-level flight to give a good centre.
- **L-11. First IMU update integrates over a huge `dt`.** [imu.c:460](../src/main/flight/imu.c#L460) initialises `previousIMUUpdateTime` to 0, so the first step is seconds long. Inherited from Betaflight and converges quickly on the bench; noted because it happens once per boot.

### Checked and found sound

- Fast 8-multiply quaternion product matches the Hamilton product (random test, max error 7e-16).
- Hold engine: shortest-path sign fix, magnitude clamp over frozen axes only, unit renormalisation, and no drift accumulation when all axes are frozen.
- AUTOHOVER `MaxRate > 0` guard on the assist trigger; roll-lock hysteresis; deadband clamps at load.
- PID anti-windup logic and its use of servo-travel saturation; yaw sign handling consistent between setpoint, MANUAL and PASSTHROUGH.
- Governor failsafe bypass and the interlock guard.
- Runtime servo trim design: trim direction folds the three reversal sources correctly.
- Mode arbitration: safety modes cleanly preempt AUTOHOVER/ATTHOLD and force a re-capture on release.

### Not reviewed

Dynamic notch and RPM filters, gyro/accelerometer drivers and calibration, `position.c` altitude estimation, `logic_condition.c`, `wiggle.c`, blackbox, MSP and CLI plumbing. The pilot-facing docs in `docs/` were audited separately.

---

## 5. Test coverage and verification plan

Unit tests exist for PID, setpoint, curves, maths and the acro trainer. The tests for the mixer, IMU and failsafe are present but **disabled** (`flight_mixer_unittest.cc.txt`, `flight_imu_unittest.cc.txt`, `flight_failsafe_unittest.cc.txt`). There are no tests for the hold engine, AUTOHOVER, ATTHOLD, the TV loop, leveling, airborne detection, servos, or nav. Every finding above is in an untested area.

Suggested order, cheapest and highest-value first:

1. **Signs (H-3, M-2).** Unit tests for `updateGpsNav()` with fixtures: aircraft south/north/east/west of target for CW and CCW, and above/below target altitude. Then one SITL confirmation of the pitch sign.
2. **Airborne (H-4).** Test the state machine directly: armed, level, sticks centred → must stay AIRBORNE if throttle is up. Then a SITL or blackbox replay of a hands-off hold.
3. **Failsafe (H-1).** Decide the intended behaviour first, then re-enable `flight_failsafe_unittest`.
4. **Hold engine.** Synthetic quaternion sequences: release mid-rotation (no snap-back), disturbance while frozen, stall timeout, per-axis independence, and a 90°/180° error.
5. **Throttle assist (M-1).** Assist must be zero with the stick at idle and with no RX signal.
6. **NaN guard (M-4).** Inject NaN into a mixer input and assert servo output stays finite.

Nothing in this document has been flight tested by the review itself.
