# SITL 3D Aircraft Model (`wingflight_3d_2m`)

The JSBSim bridge's default aircraft is a generic 2 m class 3D aerobatic RC
plane (Extra 330 / Edge 540 style) on 12S electric power. It isn't a model of
any particular kit. It is a physically consistent baseline built from typical
geometry, with every tunable in one file.

| File | Content |
|---|---|
| [scripts/jsbsim/aircraft/wingflight_3d_2m/wingflight_3d_2m.xml](../../scripts/jsbsim/aircraft/wingflight_3d_2m/wingflight_3d_2m.xml) | Geometry, mass, gear, flight controls, aerodynamics |
| [Engines/bldc_12s_200kv.xml](../../scripts/jsbsim/aircraft/wingflight_3d_2m/Engines/bldc_12s_200kv.xml) | Brushless motor (JSBSim `brushless_dc_motor`) |
| [Engines/prop_22x10e.xml](../../scripts/jsbsim/aircraft/wingflight_3d_2m/Engines/prop_22x10e.xml) | 22x10 propeller tables |
| [scripts/flightgear/Aircraft/Edge540RC](../../scripts/flightgear/Aircraft/Edge540RC) | FlightGear visual: FGAddon's Edge 540 scaled to 2 m |

`c172p` and the other models bundled with the `jsbsim` package still work:
`-Aircraft c172p` on the launcher, or `--aircraft c172p` on the bridge.

## Using it

```powershell
# One time: the Edge 540 visual for FlightGear (downloads FGAddon's ZivkoEdge into tools/)
.\scripts\sitl-jsbsim-flightgear-launch.ps1 -SetupFgAircraft

.\scripts\sitl-jsbsim-flightgear-launch.ps1 -Trim -Joystick -Configurator -FlightGear `
    -FgfsPath "C:\Program Files\FlightGear 2024.1\bin\fgfs.exe" -StopOnExit
```

The aircraft starts **on the ground** at the threshold of KSFO runway 28R,
pointing down the runway (heading 297.9°), motor idle. Arm and take off: at
full throttle it lifts off after about 5 m. If it crashes and comes to rest
nosed over or inverted, it goes back to the start after 2 s.
- **Airborne start:** `-Start air` (200 ft MSL at 40 kts, which is about 57 m
  above the runway at about 20 m/s; change with `-AltitudeFt` / `-AirspeedKts`).
- **Another start point:** `-LatDeg` / `-LonDeg` / `-HeadingDeg`.

FlightGear opens in **Tower view**, a fixed camera where an RC pilot would
stand. It is 30 m to the right of the runway and 60 m past the threshold, at
1.7 m eye height, and turns to follow the aircraft. Move it with
`-PilotAlongM` / `-PilotSideM`. `-FgView 2` gives the chase view instead, and
`V` cycles views; `X` / `Shift+X` zooms.

**Numerical robustness:** a 6.5 kg model has tiny inertias (Ixx 0.45 kg·m²).
- **Physics rate:** the bridge runs JSBSim at 4 sub-steps per 120 Hz cycle,
  i.e. 480 Hz (`--substeps`).
- **Soft contacts:** the wingtip, nose and fin contacts are kept soft
  (1500 N/m, 25 N·s/m, friction 0.5), because stiffer ones integrate to NaN on
  a wingtip strike.
- **Blow-up reset:** JSBSim's ground friction solver can still blow up in a
  violent multi-contact crash. The bridge treats body rates above 50 rad/s or
  speeds above 150 m/s as a crash and resets, so no garbage reaches SITL.

## Baseline numbers

| | Value | Basis |
|---|---|---|
| Span / wing area / MAC | 2.00 m / 0.95 m² / 0.475 m | AR 4.2, typical for 2 m 3D planes |
| Flying weight | 6.5 kg incl. 12S pack | |
| Inertia Ixx / Iyy / Izz | 0.45 / 0.85 / 1.20 kg·m² | typical radii of gyration for aerobatic models |
| Tail | H 0.22 m², V 0.14 m², arm 0.95 m | tail volume V_H 0.46, which is large, as on 3D planes |
| CG / static margin | 25 % MAC / 5 % | close to neutral, 3D setup |
| Throws | aileron ±35°, elevator ±45°, rudder ±45° | 3D rates, full stick means full throw (expo belongs in the FC or radio) |
| Servo speed | ~0.1 s/60° (rate limit 12 norm/s) | |
| Motor | Kv 200, 44.4 V, 0.08 Ω effective loop resistance, I₀ 2 A | the resistance covers motor + pack + ESC and limits peak current |
| Prop | 22x10, CT₀ 0.085, CP₀ 0.040 | shape taken from JSBSim's APC-derived DJI 9450 tables |

Aerodynamics:
- **Lift, drag and pitching moment:** tables over α −180…180°. Lift slope is
  4.35/rad with a gentle stall at 14° (CL 0.98). Beyond that it follows a
  flat plate (CD₉₀ 1.42), and tail-first flight is statically unstable.
- **Side force, yaw and roll due to sideslip:** tables over β −90…90°. The
  large fuselage side area gives knife-edge lift.
- **Rate damping:** Clp −0.45, Cmq −7.5, Cnr −0.25.
- **Control power:** saturates at large deflection. Aileron 0.30/rad,
  elevator −0.97/rad, rudder −0.14/rad.

**Prop wash:** the elevator, rudder and ailerons see a dynamic pressure that
includes the propeller slipstream. The slipstream speed along body X is
`u + 2 × prop-induced-velocity` (momentum theory). 60 % of the tail sits in
the slipstream; for the ailerons it is the inboard 25 %. Pitch and yaw damping
use the same slipstream velocity. That is what lets the model hover, torque-roll
and harrier with realistic control authority at zero airspeed. The flow
component is signed, so the controls reverse in a tail slide.

## Validation (JSBSim run offline, no SITL)

| Check | Result | Typical real 2 m 3D electric |
|---|---|---|
| Static thrust, full throttle | 13.4 kg, 7000 rpm, 117 A, 4.0 kW | 12–14 kg, 100–130 A |
| Hover throttle | ~65 % (55.6 N at 60 %, 72.8 N at 70 %, weight 63.8 N) | 50–65 % |
| Level trim | 12 m/s α 10.4° · 15 m/s 11.5 A · 20 m/s α 3.7°, 15 A · 30 m/s 28 A · 40 m/s 47 A | stall ~10–11 m/s |
| Top speed, level | ~43 m/s | 35–45 m/s |
| Vertical, full throttle from hover | +22 m/s after 3 s, still accelerating | unlimited vertical |
| Roll rate, full aileron | 293 / 444 / 616 °/s at 15 / 25 / 35 m/s | 400–700 °/s |
| Full elevator at 25 m/s | ±3.8 g, α ~50° within 0.3 s | snaps and blenders |
| Hover, hands off | torque roll ~80 °/s after 2 s | noticeable torque roll |
| Hover, full stick 0.5 s | elevator 250 °/s, rudder 195 °/s, aileron 340 °/s | full authority in prop wash |
| Hands off after trim, 5 s at 20 m/s | Δh +0.1 m, φ 0.0° | |

**Trim:** JSBSim's own `do_trim()` can't settle a `brushless_dc_motor`'s RPM
and fails with "udot doesn't appear to be trimmable". When that happens, the
bridge falls back to `settle_trim()` in
[jsbsim_bridge.py](../../scripts/jsbsim_bridge.py). Each evaluation spins the
motor up in a short free-flight run, then re-runs the initial conditions (RPM
carries over). Newton then solves α, elevator, throttle, aileron and rudder for
zero net force and moment. It converges in about 3 iterations.

## Tuning

Everything lives in `wingflight_3d_2m.xml` and the two engine files:

- **Heavier or lighter airframe:** `emptywt`, and scale the inertias
  proportionally.
- **Different motor or pack:** `velocityconstant`, `maxvolts` (use nominal
  volts), `coilresistance` (sets peak current).
- **Different prop:** `diameter`, and scale the table coefficients. Static CT
  sets thrust, CP sets current, the zero-thrust J sets pitch speed.
- **Softer or wilder:** the throws in the FCS `aerosurface_scale` ranges, or the
  control tables. Rather than detuning the model, prefer rates and expo in
  Wingflight.
- **Hover authority:** the 0.6 / 0.25 slipstream fractions in
  `aero/function/qbar-tail-psf` and `qbar-aileron-psf`.
- **Knife-edge:** the `CY_beta` table.

## FlightGear visual

FlightGear runs no physics of its own here (`--fdm=null`). It only draws what
JSBSim streams over native-FDM, and that stream also drives the surface and
propeller animations. The launcher uses `Edge540RC`, a wrapper that loads
FGAddon's `ZivkoEdge` model (Edge 540, GPL, by Torsten Dreyer) with a 0.27
scale animation and an offset that puts the Edge's CG on the streamed position.

If `tools/flightgear-aircraft/ZivkoEdge` is missing, the launcher warns and
falls back to the c172p visual. `-SetupFgAircraft` installs it.
