#!/usr/bin/env python
"""JSBSim <-> Wingflight SITL bridge.

Acts as the "simulator" side of Wingflight's TARGET=SITL UDP protocol
(src/main/target/SITL/target.c):
  - receives Wingflight's `servo_packet` (motor_speed[4] + servo[8] us) on
    <--host>:<--recv-port> (Wingflight sends here, see pwmLink in target.c),
  - drives a JSBSim flight dynamics model with those actuator values,
  - sends JSBSim's resulting state back as an `fdm_packet` to
    <--host>:<--send-port> (Wingflight listens here, see stateLink in
    target.c).

Wingflight's default fixed-wing mixer (see AGENTS.md) is:
    S1 = left aileron, S2 = right aileron (opposite sign),
    S3 = elevator, S4 = rudder, M1 = throttle.

Usage:
    tools/jsbsim-venv/Scripts/python.exe scripts/jsbsim_bridge.py

Requires the `jsbsim` package (see tools/jsbsim-venv; create it with
`scripts/sitl-jsbsim-flightgear-launch.ps1 -SetupVenv`, or manually with
`python -m venv tools/jsbsim-venv` then
`tools/jsbsim-venv/Scripts/pip.exe install -r scripts/requirements-jsbsim.txt`).
"""
import argparse
import atexit
import math
import os
import signal
import socket
import struct
import sys
import tempfile
import time

os.environ.setdefault("JSBSIM_DEBUG", "0")  # suppress verbose model-load logging

try:
    import jsbsim
except ImportError:
    sys.exit(
        "The 'jsbsim' package is not installed in this Python environment.\n"
        "Create/use the venv and install it, e.g.:\n"
        "  python -m venv tools/jsbsim-venv\n"
        "  tools/jsbsim-venv/Scripts/pip.exe install -r scripts/requirements-jsbsim.txt\n"
        "  tools/jsbsim-venv/Scripts/python.exe scripts/jsbsim_bridge.py"
    )

# Must match fdm_packet / servo_packet in src/main/target/SITL/target.h exactly
# (field order, types, native/little-endian layout).
FDM_PACKET_FMT = "<17d"
FDM_PACKET_SIZE = struct.calcsize(FDM_PACKET_FMT)
SERVO_PACKET_FMT = "<12f"
SERVO_PACKET_SIZE = struct.calcsize(SERVO_PACKET_FMT)

G_MPS2 = 9.80665
FT_TO_M = 0.3048

# S1-S4 servo array indices (0-based) in servo_packet.servo[8], per the
# default fixed-wing mixer (AGENTS.md).
S_LEFT_AILERON = 0
S_RIGHT_AILERON = 1
S_ELEVATOR = 2
S_RUDDER = 3

# target.c's refreshPwmPacket() writes motorsPwm[i] to motor_speed[i], so
# Wingflight's M1 (the wing throttle) is motor_speed[0]. A single-motor
# fixed-wing build never writes motor_speed[1..3].
DEFAULT_THROTTLE_MOTOR_INDEX = 0

# Default initial position: KSFO, which the FlightGear base package ships
# scenery for. JSBSim's own default IC is lat/lon 0/0 (open ocean, no
# FlightGear scenery), which renders as a featureless blue void.
DEFAULT_LAT_DEG = 37.6136
DEFAULT_LON_DEG = -122.3572
# ...which is KSFO's runway 28R threshold (FlightGear apt.dat: 37.61352,
# -122.35717); 28R's true heading is 297.9 deg, so the aircraft starts flying
# down the runway centreline.
DEFAULT_HEADING_DEG = 297.9

# Wingflight's own JSBSim models (scripts/jsbsim/aircraft/<name>/<name>.xml).
# A name found there wins over the models bundled with the jsbsim package.
REPO_AIRCRAFT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "jsbsim", "aircraft")
DEFAULT_AIRCRAFT = "wingflight_3d_2m"

# Per-aircraft initial altitude (ft MSL) and calibrated airspeed (kts) when
# --altitude-ft / --airspeed-kts aren't given. KSFO's field elevation is 13 ft.
AIRCRAFT_IC_DEFAULTS = {
    "wingflight_3d_2m": (200.0, 40.0),  # ~57 m AGL over the runway, ~20 m/s cruise
    "c172p": (3000.0, 90.0),
}
FALLBACK_IC_DEFAULTS = (3000.0, 90.0)

# --start default per aircraft. An RC model starts on the runway like the real
# thing (arm, take off); an airborne start hands a trimmed glider to a disarmed
# FC at zero throttle, which just crashes.
AIRCRAFT_START_DEFAULTS = {"wingflight_3d_2m": "ground"}
FALLBACK_START_DEFAULT = "air"

# Ground start attitude: CG height above ground (ft) and pitch (deg) with all
# wheels touching. wingflight_3d_2m's taildragger gear (mains 0.15 m ahead of /
# 0.28 m below the CG, tail wheel 1.15 m behind / 0.10 m below) sits 7.9 deg
# nose-up with the CG 0.257 m (0.84 ft) up; +0.1 ft so it settles, not bounces.
AIRCRAFT_GROUND_IC = {"wingflight_3d_2m": (0.95, 7.9)}

# JSBSim steps per bridge cycle (--substeps default). A 6.5 kg model's tiny
# inertias make roll damping and ground contacts stiff; at 120 Hz a hard
# wingtip strike integrates to NaN. 4 x 120 = 480 Hz physics, still 120 Hz to SITL.
AIRCRAFT_SUBSTEPS = {"wingflight_3d_2m": 4}
FALLBACK_GROUND_IC = (5.0, 0.0)  # unknown gear: drop from 5 ft, level


def euler_to_quat(phi, theta, psi):
    """Standard aerospace ZYX Euler (roll,pitch,yaw) -> earth-to-body quaternion (w,x,y,z)."""
    cphi, sphi = math.cos(phi * 0.5), math.sin(phi * 0.5)
    cth, sth = math.cos(theta * 0.5), math.sin(theta * 0.5)
    cpsi, spsi = math.cos(psi * 0.5), math.sin(psi * 0.5)
    qw = cphi * cth * cpsi + sphi * sth * spsi
    qx = sphi * cth * cpsi - cphi * sth * spsi
    qy = cphi * sth * cpsi + sphi * cth * spsi
    qz = cphi * cth * spsi - sphi * sth * cpsi
    return qw, qx, qy, qz


class SurfaceScaler:
    """Servo pulse (us) -> JSBSim [-1, 1] surface command, split around mid.

    min/mid/max default to the usual 1000/1500/2000 us servo range; override
    them (--servo-min/--servo-mid/--servo-max) if the airframe's servo config
    uses a different range, otherwise full stick deflection won't map to full
    JSBSim surface deflection.
    """

    def __init__(self, lo=1000.0, mid=1500.0, hi=2000.0):
        self.lo, self.mid, self.hi = lo, mid, hi

    def __call__(self, us):
        if us <= 0:  # unpopulated channel (target.c writes 0 past getServoCount())
            return 0.0
        if us >= self.mid:
            span = max(1e-6, self.hi - self.mid)
            return max(0.0, min(1.0, (us - self.mid) / span))
        span = max(1e-6, self.mid - self.lo)
        return max(-1.0, min(0.0, (us - self.mid) / span))


class ActuatorState:
    """Latest actuator values received from Wingflight (safe idle defaults)."""

    def __init__(self):
        self.motor_speed = [0.0, 0.0, 0.0, 0.0]
        self.servo = [1500.0] * 8

    def update_from_packet(self, data):
        values = struct.unpack(SERVO_PACKET_FMT, data)
        self.motor_speed = list(values[0:4])
        self.servo = list(values[4:12])


class MspGpsFeeder:
    """Feeds JSBSim's position/velocity to Wingflight as MSP GPS data.

    Wingflight's `fdm_packet` carries no geodetic position, and target.c
    ignores what position it does carry (apart from the baro derivation), so
    GPS-dependent firmware is untestable over the UDP link alone. This feeder
    closes that gap through the front door instead: it connects to a SITL MSP
    TCP port and pushes MSP_SET_RAW_GPS frames (fix, numSat, lat/lon, altitude,
    ground speed), which gps.c consumes when the GPS provider is set to MSP
    (SITL's config default, see src/main/target/SITL/config.c).

    Needs its OWN MSP port: SITL's per-port TCP MSP server accepts one client
    at a time, and 5761 (UART1) is usually taken by the RC/telemetry client.
    SITL's config default opens a second MSP port on 5762 (UART2). Frames are
    fire-and-forget; responses are drained and discarded. Connection failures
    retry forever without disturbing the physics loop.
    """

    MSP_SET_GPS_CONFIG = 223
    MSP_SET_RAW_GPS = 201
    GPS_PROVIDER_MSP = 2  # gpsProvider_e in src/main/pg/gps.h

    def __init__(self, host, port, rate_hz, set_provider=True):
        self.host = host
        self.port = port
        self.interval = 1.0 / max(0.1, rate_hz)
        self.set_provider = set_provider
        self.sock = None
        self.next_send = 0.0
        self.next_connect = 0.0
        self.warned = False

    @staticmethod
    def _frame(cmd, payload):
        frame = bytearray(b"$M<")
        frame.append(len(payload))
        frame.append(cmd)
        ck = len(payload) ^ cmd
        for b in payload:
            ck ^= b
        frame += payload
        frame.append(ck)
        return bytes(frame)

    def _connect(self, now):
        if now < self.next_connect:
            return
        self.next_connect = now + 2.0
        try:
            self.sock = socket.create_connection((self.host, self.port), timeout=0.5)
            self.sock.setblocking(False)
            if self.set_provider:
                # Runtime-only (not saved to EEPROM): makes the feed work even
                # against an older eeprom.bin whose provider is not MSP yet.
                cfg = struct.pack("<BBBB", self.GPS_PROVIDER_MSP, 0, 1, 0)
                self.sock.sendall(self._frame(self.MSP_SET_GPS_CONFIG, cfg))
            print(f"[jsbsim-bridge] MSP GPS feed connected to {self.host}:{self.port}")
            self.warned = False
        except OSError as exc:
            self.sock = None
            if not self.warned:
                print(f"[jsbsim-bridge] MSP GPS feed: cannot connect to {self.host}:{self.port} ({exc}) - "
                      "retrying in the background (older eeprom.bin without the UART2 MSP port? "
                      "delete it / use -FreshEeprom to pick up SITL's config defaults)")
                self.warned = True

    def update(self, fdm, now):
        if self.sock is None:
            self._connect(now)
            return
        if now < self.next_send:
            return
        self.next_send = now + self.interval

        lat = int(fdm.get_property_value("position/lat-geod-deg") * 1e7)
        lon = int(fdm.get_property_value("position/long-gc-deg") * 1e7)
        alt_m = int(max(0.0, min(65535.0, fdm.get_property_value("position/h-sl-ft") * FT_TO_M)))
        speed_cms = int(max(0.0, min(65535.0, fdm.get_property_value("velocities/vg-fps") * 30.48)))
        payload = struct.pack("<BBiiHH", 1, 10, lat, lon, alt_m, speed_cms)

        try:
            self.sock.sendall(self._frame(self.MSP_SET_RAW_GPS, payload))
            # Drain replies so the TCP buffer never fills.
            while True:
                try:
                    if not self.sock.recv(4096):
                        raise OSError("connection closed")
                except BlockingIOError:
                    break
        except OSError:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None
            print("[jsbsim-bridge] MSP GPS feed lost - reconnecting ...")


def drain_actuator_updates(sock, state):
    """Non-blocking: apply the most recent servo_packet available, if any."""
    received = False
    while True:
        try:
            data, _ = sock.recvfrom(SERVO_PACKET_SIZE)
        except BlockingIOError:
            break
        except (ConnectionResetError, OSError):
            # Windows: a UDP send to a port with no listener can surface later
            # as WSAECONNRESET on this socket. Harmless here - keep going.
            break
        if len(data) == SERVO_PACKET_SIZE:
            state.update_from_packet(data)
            received = True
    return received


def apply_actuators(fdm, state, scale, cfg):
    aileron = 0.5 * (scale(state.servo[S_LEFT_AILERON]) - scale(state.servo[S_RIGHT_AILERON]))
    fdm["fcs/aileron-cmd-norm"] = cfg.aileron_sign * aileron
    fdm["fcs/elevator-cmd-norm"] = cfg.elevator_sign * scale(state.servo[S_ELEVATOR])
    fdm["fcs/rudder-cmd-norm"] = cfg.rudder_sign * scale(state.servo[S_RUDDER])
    fdm["fcs/throttle-cmd-norm"] = max(0.0, min(1.0, state.motor_speed[cfg.throttle_index]))


def build_fdm_packet(fdm, initial_altitude_ft):
    phi = fdm.get_property_value("attitude/phi-rad")
    theta = fdm.get_property_value("attitude/theta-rad")
    psi = fdm.get_property_value("attitude/psi-rad")
    qw, qx, qy, qz = euler_to_quat(phi, theta, psi)

    # JSBSim's n-pilot-*-norm are load factors (specific force / g) in the
    # standard aerospace body frame (X-fwd, Y-right, Z-down) -- directly what
    # an accelerometer senses, matching the m/s^2 NED-body-frame value
    # target.c's updateState() expects (it applies its own sign convention).
    return struct.pack(
        FDM_PACKET_FMT,
        fdm.get_sim_time(),
        fdm.get_property_value("velocities/p-rad_sec"),
        fdm.get_property_value("velocities/q-rad_sec"),
        fdm.get_property_value("velocities/r-rad_sec"),
        fdm.get_property_value("accelerations/n-pilot-x-norm") * G_MPS2,
        fdm.get_property_value("accelerations/n-pilot-y-norm") * G_MPS2,
        fdm.get_property_value("accelerations/n-pilot-z-norm") * G_MPS2,
        qw, qx, qy, qz,
        fdm.get_property_value("velocities/v-north-fps") * FT_TO_M,
        fdm.get_property_value("velocities/v-east-fps") * FT_TO_M,
        fdm.get_property_value("velocities/v-down-fps") * FT_TO_M,
        # Position relative to the initial condition, NED, meters. JSBSim's
        # distance-from-start properties are an approximation (magnitude
        # along lat/lon directions, not a rigorous local-tangent-plane
        # projection) -- good enough since target.c only consumes the "down"
        # component (as a relative barometric altitude).
        fdm.get_property_value("position/distance-from-start-lat-mt"),
        fdm.get_property_value("position/distance-from-start-lon-mt"),
        -(fdm.get_property_value("position/h-sl-ft") - initial_altitude_ft) * FT_TO_M,
    )


# Beyond these the state is a numerical blow-up, not flight: a 3D model's
# snap roll peaks around 25 rad/s. JSBSim's ground friction solver can emit
# 1e5 lbf gear forces for a single step when a light model hits the ground
# on several contacts at once (a hard crash) - the rates explode first.
MAX_SANE_RATE_RAD_S = 50.0
MAX_SANE_SPEED_FPS = 150.0 / FT_TO_M


def state_is_sane(fdm, min_altitude_ft):
    """False once JSBSim's state has gone non-finite, blown up or hit the ground.

    Either way the FDM stops producing usable IMU data (NaNs propagate
    straight into Wingflight's fake gyro/acc via updateState()), so the
    caller re-runs the initial conditions instead of streaming garbage.
    """
    for prop in ("attitude/phi-rad", "attitude/theta-rad", "attitude/psi-rad",
                 "velocities/p-rad_sec", "velocities/q-rad_sec", "velocities/r-rad_sec",
                 "accelerations/n-pilot-x-norm", "accelerations/n-pilot-z-norm",
                 "position/h-sl-ft"):
        if not math.isfinite(fdm.get_property_value(prop)):
            return False
    for prop in ("velocities/p-rad_sec", "velocities/q-rad_sec", "velocities/r-rad_sec"):
        if abs(fdm.get_property_value(prop)) > MAX_SANE_RATE_RAD_S:
            return False
    if fdm.get_property_value("velocities/vt-fps") > MAX_SANE_SPEED_FPS:
        return False
    return fdm.get_property_value("position/h-agl-ft") > min_altitude_ft


class CrashDetector:
    """Detects a crash that doesn't end below ground: the aircraft is resting on
    its nose, back or a wingtip (structure contacts keep it from sinking, so
    state_is_sane() never fires). Requires being near the ground, nearly
    stationary and nosed over (pitch below -30 deg) or rolled past 60 deg for
    hold_s - which a hover (nose up) or a normal landing never is.
    """

    def __init__(self, hold_s=2.0, near_ground_ft=5.0, max_speed_fps=10.0):
        self.hold_s = hold_s
        self.near_ground_ft = near_ground_ft
        self.max_speed_fps = max_speed_fps
        self.since = None

    def reset(self):
        self.since = None

    def update(self, fdm):
        crashed_pose = (
            fdm.get_property_value("position/h-agl-ft") < self.near_ground_ft
            and fdm.get_property_value("velocities/vt-fps") < self.max_speed_fps
            and (abs(fdm.get_property_value("attitude/phi-deg")) > 60.0
                 or fdm.get_property_value("attitude/theta-deg") < -30.0)
        )
        now = fdm.get_sim_time()
        if not crashed_pose:
            self.since = None
            return False
        if self.since is None:
            self.since = now
        return now - self.since >= self.hold_s


def _solve_linear(a, b):
    """Gaussian elimination with partial pivoting; None if singular."""
    n = len(b)
    m = [row[:] + [b[i]] for i, row in enumerate(a)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(m[r][c]))
        if abs(m[p][c]) < 1e-12:
            return None
        m[c], m[p] = m[p], m[c]
        for r in range(n):
            if r != c:
                f = m[r][c] / m[c][c]
                for k in range(c, n + 1):
                    m[r][k] -= f * m[c][k]
    return [m[i][n] / m[i][i] for i in range(n)]


def settle_trim(fdm, spinup_s=0.5, iterations=12):
    """Straight-and-level trim that also works for electric motors.

    JSBSim's do_trim() can't settle a brushless_dc_motor's RPM (it reports
    "udot doesn't appear to be trimmable"). Here every evaluation spins the
    motor up in a short free-flight run, then re-runs the same initial
    conditions - prop RPM and actuator positions carry over run_ic() - and
    reads the net forces/moments. Newton on alpha, elevator, throttle,
    aileron and rudder drives body X/Z force (incl. gravity) and the roll,
    pitch and yaw moments to zero, so the motor's torque is trimmed out too.
    Returns (converged, [alpha_deg, elevator, throttle, aileron, rudder]).
    """
    steps = max(1, int(spinup_s / fdm.get_delta_t()))
    weight = fdm.get_property_value("inertia/weight-lbs")
    cbar = fdm.get_property_value("metrics/cbarw-ft")
    span = fdm.get_property_value("metrics/bw-ft")

    def apply(x):
        fdm["ic/alpha-deg"] = x[0]
        fdm["ic/gamma-deg"] = 0.0
        fdm.run_ic()
        fdm["fcs/elevator-cmd-norm"] = x[1]
        fdm["fcs/throttle-cmd-norm"] = x[2]
        fdm["fcs/aileron-cmd-norm"] = x[3]
        fdm["fcs/rudder-cmd-norm"] = x[4]

    def residual(x):
        apply(x)
        for _ in range(steps):
            fdm.run()
        fdm.run_ic()
        theta = math.radians(fdm.get_property_value("attitude/theta-deg"))
        return [
            (fdm.get_property_value("forces/fbx-total-lbs") - weight * math.sin(theta)) / weight,
            (fdm.get_property_value("forces/fbz-total-lbs") + weight * math.cos(theta)) / weight,
            fdm.get_property_value("moments/m-total-lbsft") / (weight * cbar),
            fdm.get_property_value("moments/l-total-lbsft") / (weight * span),
            fdm.get_property_value("moments/n-total-lbsft") / (weight * span),
        ]

    x = [2.0, 0.0, 0.5, 0.0, 0.0]
    delta = [0.5, 0.05, 0.05, 0.05, 0.05]
    lo = [-10.0, -1.0, 0.0, -1.0, -1.0]
    hi = [20.0, 1.0, 1.0, 1.0, 1.0]
    r = residual(x)
    for _ in range(iterations):
        if max(abs(v) for v in r[:2]) < 1e-3 and max(abs(v) for v in r[2:]) < 1e-4:
            break
        jac = [[0.0] * 5 for _ in range(5)]
        for j in range(5):
            xp = list(x)
            xp[j] += delta[j]
            rp = residual(xp)
            for i in range(5):
                jac[i][j] = (rp[i] - r[i]) / delta[j]
        dx = _solve_linear(jac, [-v for v in r])
        if dx is None:
            break
        x = [min(hi[i], max(lo[i], x[i] + dx[i])) for i in range(5)]
        r = residual(x)
    converged = max(abs(v) for v in r[:2]) < 5e-3 and max(abs(v) for v in r[2:]) < 5e-4
    apply(x)
    return converged, x


def apply_initial_conditions(fdm, args):
    # Geodetic, like GPS, FlightGear and airport data. JSBSim's geocentric
    # latitude differs by up to ~0.19 deg (~21 km north at KSFO).
    fdm["ic/lat-geod-deg"] = args.lat_deg
    # JSBSim's ground is flat at this elevation; match the FlightGear runway so
    # gear contact (and the crash auto-reset) happens where the scenery is.
    fdm["ic/terrain-elevation-ft"] = args.terrain_elevation_ft
    fdm["ic/long-gc-deg"] = args.lon_deg
    fdm["ic/psi-true-deg"] = args.heading_deg
    if args.start == "ground":
        h_agl_ft, theta_deg = AIRCRAFT_GROUND_IC.get(args.aircraft, FALLBACK_GROUND_IC)
        fdm["ic/vc-kts"] = 0.0
        fdm["ic/h-agl-ft"] = h_agl_ft
        fdm["ic/theta-deg"] = theta_deg
    else:
        fdm["ic/h-sl-ft"] = args.altitude_ft
        fdm["ic/vc-kts"] = args.airspeed_kts
        fdm["ic/alpha-deg"] = 0.0
        fdm["ic/gamma-deg"] = 0.0
    fdm.run_ic()

    if not args.no_engine_start:
        # A JSBSim piston model (c172p and friends) boots with the engine cold
        # and stopped, so fcs/throttle-cmd-norm produces exactly zero thrust no
        # matter what Wingflight's M1 output does. -1 means "all engines".
        fdm["propulsion/set-running"] = -1

    if args.start == "ground":
        print("[jsbsim-bridge] ground start: on the runway, engine idle - arm and take off")
        return

    if args.trim:
        try:
            fdm.do_trim(args.trim_mode)
            print(f"[jsbsim-bridge] trimmed (mode {args.trim_mode})")
            return
        except Exception as exc:  # JSBSim raises a plain RuntimeError on failure
            print(f"[jsbsim-bridge] JSBSim trim failed ({exc}); trying settle trim (electric motors) ...")
        converged, x = settle_trim(fdm)
        state = "trimmed" if converged else "WARNING: settle trim did not converge, best effort"
        print(f"[jsbsim-bridge] {state}: alpha {x[0]:.2f} deg, elevator {x[1]:+.3f}, throttle {x[2]:.3f}, "
              f"aileron {x[3]:+.3f}, rudder {x[4]:+.3f}")


def write_flightgear_output_directive(host, port, rate):
    """Write a JSBSim output-directives file streaming to FlightGear's native
    FDM UDP protocol (JSBSim serializes FlightGear's net_fdm struct itself --
    see FGOutputFG in JSBSim). Must be registered via set_output_directive()
    *before* load_model() (JSBSim ignores it if called after).
    """
    xml = (
        '<?xml version="1.0"?>\n'
        f'<output name="{host}" type="FLIGHTGEAR" protocol="UDP" port="{port}" rate="{rate}"/>\n'
    )
    fd, path = tempfile.mkstemp(prefix="jsbsim_fg_output_", suffix=".xml")
    with os.fdopen(fd, "w") as f:
        f.write(xml)
    atexit.register(lambda: os.path.exists(path) and os.unlink(path))
    return path


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1", help="Wingflight SITL host (default: 127.0.0.1)")
    parser.add_argument("--recv-port", type=int, default=9002, help="Port to receive servo_packet on (default: 9002)")
    parser.add_argument("--send-port", type=int, default=9003, help="Port to send fdm_packet to (default: 9003)")
    parser.add_argument("--aircraft", default=DEFAULT_AIRCRAFT,
                        help=f"JSBSim aircraft model name (default: {DEFAULT_AIRCRAFT}, the generic 2 m 3D model in "
                             "scripts/jsbsim/aircraft; c172p and the other jsbsim-package models also work)")
    parser.add_argument("--rate", type=float, default=120.0, help="Simulation/send rate in Hz (default: 120)")
    parser.add_argument("--substeps", type=int, default=None,
                        help="JSBSim integration steps per --rate cycle (default: 4 for wingflight_3d_2m, else 1)")
    parser.add_argument("--altitude-ft", type=float, default=None,
                        help="Initial altitude, ft MSL (default: per aircraft - 200 for wingflight_3d_2m, 3000 otherwise)")
    parser.add_argument("--airspeed-kts", type=float, default=None,
                        help="Initial calibrated airspeed, kts (default: per aircraft - 40 for wingflight_3d_2m, 90 otherwise)")
    parser.add_argument("--lat-deg", type=float, default=DEFAULT_LAT_DEG,
                        help=f"Initial latitude, deg (default: {DEFAULT_LAT_DEG}, KSFO - FlightGear ships scenery there)")
    parser.add_argument("--lon-deg", type=float, default=DEFAULT_LON_DEG,
                        help=f"Initial longitude, deg (default: {DEFAULT_LON_DEG})")
    parser.add_argument("--heading-deg", type=float, default=DEFAULT_HEADING_DEG,
                        help=f"Initial true heading, deg (default: {DEFAULT_HEADING_DEG}, along KSFO runway 28R)")
    parser.add_argument("--terrain-elevation-ft", type=float, default=13.0,
                        help="Ground elevation JSBSim assumes, ft MSL (default: 13 = KSFO field elevation)")
    parser.add_argument("--start", choices=["air", "ground"], default=None,
                        help="Start airborne at --altitude-ft/--airspeed-kts, or at rest on the runway "
                             "(default: ground for wingflight_3d_2m, air otherwise)")
    parser.add_argument("--trim", action="store_true",
                        help="Trim the aircraft for steady flight at the initial conditions before running (air start only)")
    parser.add_argument("--no-engine-start", action="store_true",
                        help="Leave the engine(s) stopped (by default they are started, otherwise throttle does nothing)")
    parser.add_argument("--trim-mode", type=int, default=1,
                        help="JSBSim trim mode passed to do_trim() (default: 1 = full trim)")
    parser.add_argument("--throttle-motor-index", type=int, default=DEFAULT_THROTTLE_MOTOR_INDEX,
                        choices=[0, 1, 2, 3],
                        help="servo_packet.motor_speed[] index carrying M1/throttle "
                             f"(default: {DEFAULT_THROTTLE_MOTOR_INDEX} = M1)")
    parser.add_argument("--servo-min", type=float, default=1000.0, help="Servo pulse at full negative deflection, us (default: 1000)")
    parser.add_argument("--servo-mid", type=float, default=1500.0, help="Servo pulse at neutral, us (default: 1500)")
    parser.add_argument("--servo-max", type=float, default=2000.0, help="Servo pulse at full positive deflection, us (default: 2000)")
    parser.add_argument("--invert-aileron", action="store_true", help="Flip the aileron command sign (if roll response is backwards)")
    parser.add_argument("--invert-elevator", action="store_true", help="Flip the elevator command sign")
    parser.add_argument("--invert-rudder", action="store_true", help="Flip the rudder command sign")
    parser.add_argument("--min-agl-ft", type=float, default=-5.0,
                        help="Reset the FDM when the aircraft drops below this AGL altitude (default: -5)")
    parser.add_argument("--no-auto-reset", action="store_true",
                        help="Keep streaming after a crash/NaN instead of re-running the initial conditions")
    parser.add_argument("--no-realtime", action="store_true", help="Run as fast as possible instead of pacing to wall-clock time")
    parser.add_argument("--status-interval", type=float, default=2.0, help="Seconds between status lines (default: 2)")
    parser.add_argument("--msp-gps", action="store_true",
                        help="Also feed JSBSim's position/velocity to SITL as MSP GPS data (MSP_SET_RAW_GPS) "
                             "over a second MSP TCP port (see --msp-gps-port)")
    parser.add_argument("--msp-gps-port", type=int, default=5762,
                        help="SITL MSP TCP port for the GPS feed (default: 5762 = UART2, SITL's config default; "
                             "must NOT be the port the RC/telemetry client uses - one client per port)")
    parser.add_argument("--msp-gps-rate", type=float, default=5.0, help="GPS feed rate in Hz (default: 5)")
    parser.add_argument("--no-msp-gps-config", action="store_true",
                        help="Do not set the FC's GPS provider to MSP on connect (runtime-only change, never saved)")
    parser.add_argument("--flightgear", action="store_true", help="Also stream state to FlightGear via JSBSim's native FDM UDP output")
    parser.add_argument("--fg-host", default="127.0.0.1", help="FlightGear host (default: 127.0.0.1)")
    parser.add_argument("--fg-port", type=int, default=5550, help="FlightGear --native-fdm UDP port (default: 5550)")
    parser.add_argument("--fg-rate", type=float, default=30.0, help="FlightGear output rate in Hz (default: 30)")
    parser.add_argument("--fg-aircraft", default=None,
                        help="FlightGear aircraft shown in the printed fgfs command (visual only; default: "
                             "Edge540RC for wingflight_3d_2m, else the JSBSim aircraft name)")
    args = parser.parse_args(argv)

    default_alt, default_kts = AIRCRAFT_IC_DEFAULTS.get(args.aircraft, FALLBACK_IC_DEFAULTS)
    if args.altitude_ft is None:
        args.altitude_ft = default_alt
    if args.airspeed_kts is None:
        args.airspeed_kts = default_kts
    if args.fg_aircraft is None:
        args.fg_aircraft = "Edge540RC" if args.aircraft == "wingflight_3d_2m" else args.aircraft
    if args.start is None:
        args.start = AIRCRAFT_START_DEFAULTS.get(args.aircraft, FALLBACK_START_DEFAULT)
    if args.substeps is None:
        args.substeps = AIRCRAFT_SUBSTEPS.get(args.aircraft, 1)
    args.substeps = max(1, args.substeps)

    args.aileron_sign = -1.0 if args.invert_aileron else 1.0
    args.elevator_sign = -1.0 if args.invert_elevator else 1.0
    args.rudder_sign = -1.0 if args.invert_rudder else 1.0
    args.throttle_index = args.throttle_motor_index
    return args


def main():
    args = parse_args()
    dt = 1.0 / args.rate

    # Launcher scripts redirect this process's stdout to a log file; without
    # line buffering nothing shows up there until the process exits, which
    # makes "is the bridge alive?" impossible to answer while it runs.
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except (AttributeError, ValueError):
        pass

    # Stop-Process/SIGTERM should unwind through the same path as Ctrl+C so the
    # temp output-directive file is cleaned up and the sockets are closed.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))

    fdm = jsbsim.FGFDMExec(None)
    fdm.set_dt(dt / args.substeps)

    if args.flightgear:
        directive_path = write_flightgear_output_directive(args.fg_host, args.fg_port, args.fg_rate)
        fdm.set_output_directive(directive_path)
        print(
            f"[jsbsim-bridge] FlightGear output enabled -> {args.fg_host}:{args.fg_port} "
            f"@ {args.fg_rate}Hz. Launch FlightGear with:\n"
            f"  fgfs --aircraft={args.fg_aircraft} --fdm=null "
            f"--native-fdm=socket,in,{int(args.fg_rate)},,{args.fg_port},udp "
            f"--lat={args.lat_deg} --lon={args.lon_deg} --altitude={args.altitude_ft} "
            f"--timeofday=noon --disable-real-weather-fetch --disable-clouds3d"
        )

    if os.path.isfile(os.path.join(REPO_AIRCRAFT_DIR, args.aircraft, args.aircraft + ".xml")):
        # Engine/prop files live in the model's own Engines/ folder, which
        # JSBSim searches next to the aircraft - only the aircraft path changes.
        fdm.set_aircraft_path(REPO_AIRCRAFT_DIR)
    if not fdm.load_model(args.aircraft):
        sys.exit(f"Failed to load JSBSim aircraft model '{args.aircraft}'")

    apply_initial_conditions(fdm, args)
    initial_altitude_ft = fdm.get_property_value("position/h-sl-ft")

    recv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    recv_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        recv_sock.bind((args.host, args.recv_port))
    except OSError as exc:
        sys.exit(
            f"Could not bind UDP {args.host}:{args.recv_port} ({exc}).\n"
            "Another jsbsim_bridge.py is probably still running - stop it first."
        )
    recv_sock.setblocking(False)

    send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    send_addr = (args.host, args.send_port)

    scale = SurfaceScaler(args.servo_min, args.servo_mid, args.servo_max)
    state = ActuatorState()

    gps_feeder = None
    if args.msp_gps:
        gps_feeder = MspGpsFeeder(args.host, args.msp_gps_port, args.msp_gps_rate,
                                  set_provider=not args.no_msp_gps_config)
        print(f"[jsbsim-bridge] MSP GPS feed enabled -> {args.host}:{args.msp_gps_port} @ {args.msp_gps_rate}Hz")

    print(f"[jsbsim-bridge] aircraft={args.aircraft} rate={args.rate}Hz x{args.substeps} substeps "
          f"recv={args.host}:{args.recv_port} send={args.host}:{args.send_port} "
          f"throttle=motor_speed[{args.throttle_index}] "
          f"ic=({args.lat_deg},{args.lon_deg}) hdg {args.heading_deg:.1f} "
          + (f"on the ground" if args.start == "ground" else f"{args.altitude_ft:.0f}ft {args.airspeed_kts:.0f}kts"))
    print("[jsbsim-bridge] waiting for Wingflight servo_packet updates (idle defaults until then)...")

    next_status = time.monotonic() + args.status_interval
    next_step_wall = time.monotonic()
    packets_received = 0
    resets = 0
    crash_detector = CrashDetector()
    try:
        while True:
            if drain_actuator_updates(recv_sock, state):
                packets_received += 1
            apply_actuators(fdm, state, scale, args)
            for _ in range(args.substeps):
                fdm.run()

            crashed = crash_detector.update(fdm)
            if crashed or not state_is_sane(fdm, args.min_agl_ft):
                reason = "crashed (resting nosed over / inverted)" if crashed else "FDM state invalid (crashed or non-finite)"
                if args.no_auto_reset:
                    print(f"[jsbsim-bridge] {reason} and --no-auto-reset given - stopping.")
                    break
                resets += 1
                print(f"[jsbsim-bridge] {reason} - re-running initial conditions (reset #{resets})")
                apply_initial_conditions(fdm, args)
                crash_detector.reset()

            send_sock.sendto(build_fdm_packet(fdm, initial_altitude_ft), send_addr)

            now = time.monotonic()
            if gps_feeder is not None:
                gps_feeder.update(fdm, now)
            if now >= next_status:
                print(
                    f"[jsbsim-bridge] t={fdm.get_sim_time():7.2f}s "
                    f"alt={fdm.get_property_value('position/h-sl-ft'):7.1f}ft "
                    f"ias={fdm.get_property_value('velocities/vc-kts'):5.1f}kts "
                    f"thr={fdm.get_property_value('fcs/throttle-cmd-norm'):.2f} "
                    f"ail={fdm.get_property_value('fcs/aileron-cmd-norm'):+.2f} "
                    f"ele={fdm.get_property_value('fcs/elevator-cmd-norm'):+.2f} "
                    f"rud={fdm.get_property_value('fcs/rudder-cmd-norm'):+.2f} "
                    f"packets_rx={packets_received}"
                )
                if packets_received == 0:
                    print("[jsbsim-bridge]   (no servo_packet received yet - is wingflight_SITL.elf running?)")
                next_status = now + args.status_interval

            if not args.no_realtime:
                next_step_wall += dt
                sleep_for = next_step_wall - time.monotonic()
                if sleep_for > 0:
                    time.sleep(sleep_for)
                else:
                    next_step_wall = time.monotonic()
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        recv_sock.close()
        send_sock.close()
        if gps_feeder is not None and gps_feeder.sock is not None:
            try:
                gps_feeder.sock.close()
            except OSError:
                pass
        print("\n[jsbsim-bridge] stopped")


if __name__ == "__main__":
    main()
