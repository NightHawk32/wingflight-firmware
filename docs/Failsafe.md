# Failsafe

If the radio link is lost, or the receiver fails or is disconnected, the pilot has no control of
the aircraft. **It is vitally important to check that your failsafe works before flying.**

## What Wingflight does today

> **Important.** The flight-controller failsafe state machine inherited from Betaflight is
> **disabled** in this firmware (`failsafeStartMonitoring()` in `flight/failsafe.c` is a stub).
> Betaflight's "stage 2" (auto-landing, drop and disarm, GPS Rescue on link loss) never runs, so
> `failsafe_procedure`, `failsafe_delay`, `failsafe_off_delay`, `failsafe_throttle_low_delay`,
> `failsafe_recovery_delay`, `failsafe_stick_threshold` and `failsafe_switch_mode` have no effect on
> link loss. The flight controller does **not** disarm, land or return home on its own when the link
> drops.
>
> Configure your **receiver's own failsafe**, and the channel fallback values below.

What the flight controller does when the link is lost or a channel becomes invalid:

1. **Signal validation.** A missing or dropped frame for 100 ms, or an invalid pulse length on a
   control channel, counts as a bad signal.
2. **Hold for 300 ms.** The last good value of each channel is held.
3. **Channel fallback.** After 300 ms each channel takes its configured fallback value (below).
   With the defaults, roll, pitch and yaw go to centre and throttle goes just below the
   off-throttle threshold, so the motor stops. Auxiliary channels **hold their last value**, so an
   arm switch or mode switch that was on stays on.
4. **Arming is blocked** while there is no valid signal.
5. The [governor](Governor.md) is bypassed while the link is down, so a governor switch held on
   cannot keep the motor running.

Because roll, pitch and yaw go to centre, the surfaces go neutral, or level the aircraft if ANGLE,
HORIZON or another self-levelling mode is still selected by a held aux channel. Nothing else
happens.

## Recommended setup

1. **Receiver failsafe.** Set failsafe on your receiver or transmitter to a known-safe state:
   throttle off (or idle for a glider), and a mode switch position that selects self-levelling
   (ANGLE). Because auxiliary channels hold their last value, do this
   at the receiver, not with the flight-controller fallback alone.
2. **Fallback values.** Use `rxfail` (below) for the flight-controller side.
3. **Test it.** Turn the transmitter off with the props off, and check the surfaces and the
   motor. Test a receiver that keeps sending normal-looking frames on loss separately: the
   flight controller cannot detect that case.

## Channel fallback (`rxfail`)

```
rxfail <channel> <mode> [<value>]
```

`channel` is 0-based. The modes are:

| Mode | Applies to | Meaning |
|---|---|---|
| `a` | Roll, pitch, yaw, throttle | **Auto**: roll, pitch and yaw go to centre, throttle to the failsafe throttle |
| `h` | Any channel | **Hold** the last good value |
| `s` | Any channel | **Set** to a fixed pulse width, `value` in microseconds |

Defaults are `a` for the four control channels and `h` for the rest. Run `rxfail` with no arguments
to see the current setting for every channel. To make a mode switch fall back to a known position,
set that auxiliary channel to `s` with the pulse width that selects the mode you want:

```
rxfail 4 s 1900
```

## Failsafe switch (`FAILSAFE` mode)

The `FAILSAFE` mode (see [Modes](Modes.md)) makes the flight controller treat the four control
channels as invalid. After the 300 ms hold they take their fallback values, exactly as for real
signal loss. It is useful for testing your fallback values, and as a panic switch. The switch's
stage-2 options (`failsafe_switch_mode`) do nothing.

## GPS return-to-home

The `GPS RTH` mode (see [Modes](Modes.md) and [Gps](Gps.md)) can be selected by a switch, or by a
fallback value on an auxiliary channel. It steers by banking, holds altitude by pitch, and leaves
throttle to you, so it is not a complete recovery on its own. **It is experimental**: the review in
[Flight Dynamics](FlightDynamics.md) (H-3, M-2) found that its altitude-hold direction and loiter
direction look inverted, and neither has been verified in flight. Test it in a safe area before
trusting it. The inherited `GPS RESCUE`
mode is tuned for multirotors and does not steer or control altitude on a wing. Do not rely on it.

## Details

See [Flight Dynamics](FlightDynamics.md) for how the failsafe path fits with the rest of the
control loop, including the review notes on this stub.
