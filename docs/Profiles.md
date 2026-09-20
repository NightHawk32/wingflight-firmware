# Profiles

A **profile** is a set of performance-related settings that you can switch between. There are two
kinds:

* **PID profiles** hold the PID gains, filters, leveling and hold-mode settings, throttle
  attenuation and the like. See [PID tuning](PID%20tuning.md).
* **Rate profiles** hold the stick feel: rates, expo, response time, acceleration limit and
  setpoint boost.

How many of each you get depends on the flash size of the board: **up to 6 of each** on
boards with more than 256 KB of flash, fewer on smaller ones. The first is number 1, and
`profile 0` in the CLI.

Profiles let you try a change without losing your known-good setting. Edit a spare profile on
the bench, and select it in the field. If it does not fly well, switch back.

## Changing profiles

A PID profile can be selected:

* in the Configurator,
* in the CLI: `profile <index>`, with the index starting at 0,
* with sticks, while disarmed (profiles 1 to 3 only). See [Controls](Controls.md),
* with an [in-flight adjustment](Inflight%20Adjustments.md), using a switch on the transmitter.

A rate profile can be selected in the Configurator, in the CLI with `rateprofile <index>`, with
sticks when neither ANGLE nor HORIZON is active (see [Controls](Controls.md)), or with an
adjustment.

Changes made in a profile are not stored until you `save`. Give a PID profile a name with
`set profile_name = <name>` to tell them apart.

## CLI

```
profile 1
dump profile
```

prints the settings of PID profile 1 in a form you can paste back later. `dump rates` does the
same for the current rate profile, after `rateprofile <index>`. To back up everything, see
[Cli](Cli.md).

## Thrust-vector profiles

The independent thrust-vector loop (`feature THRUST_VECTOR`) has its own six profiles, selected
with `tv_profile <index>` or the `TV_PROFILE` adjustment.
