/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "platform.h"

#ifdef USE_ACC

#include "build/build_config.h"
#include "build/debug.h"

#include "config/config.h"

#include "common/axis.h"
#include "common/maths.h"

#include "flight/airborne.h"
#include "flight/imu.h"
#include "flight/setpoint.h"

#include "autohover.h"

// Below this combined pitch+yaw error, the aircraft is considered close enough to vertical that
// the quaternion error's per-axis decomposition can be trusted for roll-hold tracking (see the
// nearVerticalTarget comment in autoHoverApply). Not a user-facing tunable -- an internal
// numerical-stability margin, picked well below the angle range where axis coupling becomes
// significant while still being loose enough to engage roll-hold shortly before reaching upright.
#define AUTOHOVER_ROLL_HOLD_ENTRY_DEG 30.0f

// Quaternion-based vertical (90 degree pitch) attitude + heading hold, for 3D "prop hang" hover.
// Deliberately NOT built on leveling.c's Euler-angle approach -- that computes roll/pitch error
// per axis independently and hits gimbal lock exactly at 90 degrees pitch, the one attitude this
// feature lives at. Roll and yaw become degenerate in Euler terms right at the moment control
// matters most, so leveling.c's angleModeApply/horizonModeApply cannot simply be re-aimed here.
//
// Pitch and yaw are always attitude-held (they reposition the nose horizontally while vertical).
// Roll is free while the stick is deflected -- in this vertical attitude the aircraft's roll axis
// coincides with the world vertical axis, so roll is the pilot's spin/pirouette control, the same
// role yaw plays in normal nose-level flight (compare angleModeApply/horizonModeApply, which
// likewise leave yaw unheld). But unlike a bare pass-through, roll is also held once the stick
// returns to center: the instant it goes idle, the current roll is captured, and disturbance-
// driven drift away from that captured value (e.g. torque roll) is corrected back -- the same
// deadband track/freeze pattern atthold.c uses for all three axes, just scoped to roll alone
// here, since pitch/yaw already have their own always-on vertical-attitude target below.
//
// Known limitations (documented, not fixed here):
// - Does not subtract accelerometerConfig()->accelerometerTrims like leveling.c/trainer.c do, so a
//   pilot with board-mount trim dialled in will hover systematically off-vertical by that amount.
// - The held heading is captured from attitude.values.yaw, which is itself derived from an atan2
//   that degrades in conditioning as pitch->90 degrees -- re-engaging while already near-vertical
//   can capture a noisy heading. Heading is not well-defined exactly vertical; this is inherent.
// - Attitude/heading hold only -- no GPS or optical-flow position lock. Horizontal drift is the
//   pilot's responsibility via normal stick input, same as deflecting away from ANGLE_MODE's level
//   target and letting go to spring back.
// - Manual throttle only. This commands an attitude, not a maneuver profile -- it has no awareness
//   of airspeed/energy state or whether the airframe has enough thrust to sustain a vertical hover.
//   If it doesn't, engaging this commands a hard (rate-clamped) pitch-up and the aircraft will
//   likely stall/tumble rather than hover.

typedef struct {
    bool    Active;
    float   Gain;
    float   MaxAngle;   // degrees the stick may deflect the target off vertical/held heading
    float   MaxRate;    // deg/s clamp on the commanded attitude-capture rate (safety limit)
    int16_t HeadingTargetDecidegrees;
    bool    RollCaptured;           // false until the held roll has snapped to the current attitude
                                     // at least once since engagement (avoids a snap-to-zero if the
                                     // roll stick happens to already be centered on engage)
    float   RollDeadband;           // fraction (0..1) of roll stick deflection that keeps the held
                                     // roll tracking current attitude (no correction)
    float   RollOffsetDecidegrees;  // held roll offset from qBase's canonical roll=0, updated while
                                     // tracking, frozen (and corrected back to) while idle
} autoHover_t;

static FAST_DATA_ZERO_INIT autoHover_t autoHover;

int get_ADJUSTMENT_AUTOHOVER_GAIN(void)
{
    return currentPidProfile->autohover.gain;
}

void set_ADJUSTMENT_AUTOHOVER_GAIN(int value)
{
    currentPidProfile->autohover.gain = value;
    autoHover.Gain = value / 10.0f;
}

INIT_CODE void autoHoverInit(const pidProfile_t *pidProfile)
{
    autoHover.Gain = pidProfile->autohover.gain / 10.0f;
    autoHover.MaxAngle = pidProfile->autohover.max_angle;
    autoHover.MaxRate = pidProfile->autohover.max_rate;
    autoHover.RollDeadband = pidProfile->autohover.roll_deadband / 100.0f;
}

// Called once on the rising edge of AUTOHOVER_MODE so the held heading is captured fresh each
// time the mode engages -- mirrors how the (now-removed) attitude-hold mode used to capture its
// target on activation.
void autoHoverSetState(bool state)
{
    if (state && !autoHover.Active) {
        autoHover.HeadingTargetDecidegrees = attitude.values.yaw;
        autoHover.RollOffsetDecidegrees = 0.0f;
        autoHover.RollCaptured = false;
    }

    autoHover.Active = state;
}

float autoHoverApply(int axis, float pidSetpoint)
{
    static float rate[3];

    if (!autoHover.Active) {
        return pidSetpoint;
    }

    if (axis == FD_ROLL) {
        // Roll is free while the stick is deflected -- in this vertical attitude the aircraft's
        // roll axis coincides with the world vertical axis, so roll is the pilot's spin/pirouette
        // control, the same role yaw plays in normal (nose-level) flight. angleModeApply/
        // horizonModeApply leave yaw as a raw pass-through for exactly this reason (see
        // leveling.c); roll gets the same treatment here while active, so a held aileron
        // deflection produces continuous rotation instead of converging on a bounded offset and
        // fighting the stick. Once the stick returns to center, roll is captured and held instead
        // (see rollActive below) -- disturbance-driven drift no longer goes uncorrected.
        const bool rollActive = !autoHover.RollCaptured
            || fabsf(getDeflection(FD_ROLL)) > autoHover.RollDeadband;

        // Held target: vertical, at the captured heading, plus the pilot's pitch/yaw stick
        // deflection as a small local (body-frame) rotation offset -- same "deflect away from
        // the hold and spring back when centred" feel as angleModeApply, just centred on
        // vertical instead of level. RollOffsetDecidegrees carries the held roll (0 until the
        // stick has centered at least once); it's folded straight into qBase, rather than added
        // as a qStickOffset perturbation like pitch/yaw, because it must survive many loops
        // frozen at whatever value it last captured, not follow the stick every loop.
        // Bench-confirmed: +900 here drives the elevator toward nose-down, not nose-up (the
        // stabilisation loop itself is correct -- verified via blackbox, axisP/axisF go strongly
        // positive on engage exactly as intended -- it was just chasing the wrong target). -900
        // is the physically-vertical, nose-up target.
        quaternion qBase;
        imuEulerToQuaternion((int16_t)lrintf(autoHover.RollOffsetDecidegrees), -900, autoHover.HeadingTargetDecidegrees, &qBase);

        const float pitchOffset = DEGREES_TO_RADIANS(autoHover.MaxAngle * getDeflection(FD_PITCH));
        const float yawOffset   = DEGREES_TO_RADIANS(autoHover.MaxAngle * getDeflection(FD_YAW));

        quaternion qStickOffset = {
            .w = 1.0f,
            .x = 0.0f,
            .y = pitchOffset * 0.5f,
            .z = yawOffset * 0.5f,
        };
        const float stickNormRecip = 1.0f / sqrtf(sq(qStickOffset.w) + sq(qStickOffset.x) + sq(qStickOffset.y) + sq(qStickOffset.z));
        qStickOffset.w *= stickNormRecip;
        qStickOffset.x *= stickNormRecip;
        qStickOffset.y *= stickNormRecip;
        qStickOffset.z *= stickNormRecip;

        // Right-multiply: the stick offset is a perturbation local to the base target, not the
        // world frame. This is the deliberate choice -- it keeps stick response feeling the same
        // regardless of which way the held heading points. The other composition order would make
        // roll-stick response depend on the held heading, which would feel wrong.
        quaternion qTarget;
        imuQuaternionMultiplication(&qBase, &qStickOffset, &qTarget);

        // Defensive renormalize -- qBase and qStickOffset are each unit, so qTarget should already
        // be unit up to floating point drift, but this is cheap and matches how imu.c renormalizes
        // its own quaternion every iteration.
        const float targetNormRecip = 1.0f / sqrtf(sq(qTarget.w) + sq(qTarget.x) + sq(qTarget.y) + sq(qTarget.z));
        qTarget.w *= targetNormRecip;
        qTarget.x *= targetNormRecip;
        qTarget.y *= targetNormRecip;
        qTarget.z *= targetNormRecip;

        quaternion qCurrent;
        getQuaternion(&qCurrent);

        quaternion qCurrentConj = { .w = qCurrent.w, .x = -qCurrent.x, .y = -qCurrent.y, .z = -qCurrent.z };

        quaternion qError;
        imuQuaternionMultiplication(&qCurrentConj, &qTarget, &qError);

        // Shortest-path sign correction -- q and -q represent the same physical rotation, but
        // without this the error can decompose onto the "long way around" axis instead of the
        // direct one.
        if (qError.w < 0.0f) {
            qError.w = -qError.w;
            qError.x = -qError.x;
            qError.y = -qError.y;
            qError.z = -qError.z;
        }

        // Standard geometric attitude-control error term (2 * vector part) -- valid and
        // singularity-free across the full 0-180 degree range, unlike an acos/axis-angle
        // decomposition (which needs its own shortest-path check plus a division that blows up as
        // the error angle approaches zero). Magnitude saturates smoothly toward 2.0 rad as the true
        // error approaches 180 degrees, rather than growing unbounded. Index 0 (roll) measures
        // error against qBase's held roll offset above rather than a fixed reference -- same
        // singularity-free trick as pitch/yaw, just aimed at a value that tracks-then-freezes
        // instead of sitting fixed.
        float errorDeg[3] = {
            (2.0f * qError.x) / M_RADf,
            (2.0f * qError.y) / M_RADf,
            (2.0f * qError.z) / M_RADf,
        };

        // errorDeg[0] is only a clean, independent measure of roll drift once the aircraft is
        // already close to the vertical target -- quaternion rotations don't decompose into
        // independent per-axis components for a large total error (e.g. engaging from level on
        // the bench, ~90 degrees of pitch away), so at that distance errorDeg[0] picks up
        // pitch/yaw's error instead of genuine roll drift. Left unguarded, that spurious value
        // feeds into the persistent RollOffsetDecidegrees integrator below, which shifts qBase's
        // roll next loop, which changes next loop's error again -- a real, bench-confirmed
        // runaway feedback loop (heavy servo oscillation, pitch never settling into a clean
        // nose-up command) with no actual roll disturbance behind it. Computed from the raw,
        // pre-attenuation error (below) since that's what genuinely reflects how far from
        // vertical the aircraft still is, on the ground or in the air alike.
        const bool nearVerticalTarget = sqrtf(sq(errorDeg[1]) + sq(errorDeg[2])) < AUTOHOVER_ROLL_HOLD_ENTRY_DEG;

        // Same pre-airborne attenuation angleModeApply/horizonModeApply use, so the switch can be
        // armed/tested on the ground without snapping at full strength -- reduced authority, not
        // the zero authority forcing rollActive true unconditionally pre-airborne used to give.
        // Roll is included here now too: RollCaptured still forces the first post-engage loop to
        // track regardless of ground state, so there's no snap-to-a-stale-offset risk from
        // removing that forced-tracking behavior.
        if (!isAirborne()) {
            errorDeg[0] *= 0.25f;
            errorDeg[1] *= 0.25f;
            errorDeg[2] *= 0.25f;
        }

        if (!nearVerticalTarget) {
            // Still transitioning to vertical -- defer the whole roll-hold state machine (it's
            // only meant to reject torque roll once already hovering, not guide the initial snap
            // to vertical) and leave RollCaptured false so the first loop after crossing into
            // range below tracks (captures current roll) rather than freezing on a stale offset.
            rate[FD_ROLL] = pidSetpoint;
        } else if (rollActive) {
            // Track: keep the held roll offset following the current attitude, so a future freeze
            // starts from ~zero error instead of snapping. This only ever adds a relative,
            // singularity-free error term onto the running offset -- it never reads an absolute
            // Euler roll angle, which would be undefined right at this vertical attitude (the
            // classic gimbal-lock problem this file's quaternion approach exists to avoid). Wrapped
            // to +-180 degrees since only qBase's periodic quaternion construction cares about the
            // value, not any notion of accumulated turn count.
            autoHover.RollOffsetDecidegrees += errorDeg[0] * 10.0f;
            if (autoHover.RollOffsetDecidegrees > 1800.0f) {
                autoHover.RollOffsetDecidegrees -= 3600.0f;
            } else if (autoHover.RollOffsetDecidegrees < -1800.0f) {
                autoHover.RollOffsetDecidegrees += 3600.0f;
            }

            rate[FD_ROLL] = pidSetpoint;
            autoHover.RollCaptured = true;
        } else {
            // Frozen: correct back toward the captured roll. A scalar clamp, kept separate from
            // the pitch/yaw vector clamp below -- roll's correction isn't part of that rotation.
            rate[FD_ROLL] = constrainf(errorDeg[0] * autoHover.Gain, -autoHover.MaxRate, autoHover.MaxRate);
            autoHover.RollCaptured = true;
        }

        float magnitude = 0.0f;
        for (int i = FD_PITCH; i <= FD_YAW; i++) {
            rate[i] = errorDeg[i] * autoHover.Gain;
            magnitude += sq(rate[i]);
        }
        magnitude = sqrtf(magnitude);

        // Clamp the vector's magnitude, not each axis independently -- per-axis clamping would
        // distort the rotation axis mid-maneuver (e.g. pitch saturating before yaw), turning a
        // clean single-axis snap-to-vertical into a curved one. Roll is excluded -- it has its own
        // scalar clamp above and isn't part of this rotation vector.
        if (magnitude > autoHover.MaxRate && magnitude > 0.0f) {
            const float scale = autoHover.MaxRate / magnitude;
            rate[FD_PITCH] *= scale;
            rate[FD_YAW] *= scale;
        }
    }

    DEBUG_AXIS(AUTOHOVER, axis, 0, rate[axis]);

    return rate[axis];
}

#endif
