/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Betaflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "platform.h"

#ifdef USE_PWM_OUTPUT

#include "build/debug.h"
#include "build/debug_pin.h"

#include "common/maths.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/motor.h"
#include "drivers/pwm_output.h"
#include "drivers/time.h"
#include "drivers/timer.h"
#include "pg/motor.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#include "platform/pwm.h"

#define TOPMAX (0xfffe) // maximum practical TOP value to allow for 0% to 100% duty cycle
#define LEVELMAX (TOPMAX + 1) // maximum comparison value

FAST_DATA_ZERO_INIT pwmOutputPort_t motors[MAX_SUPPORTED_MOTORS];
static picoPwmOutput_t picoPwmMotors[MAX_SUPPORTED_MOTORS];
static bool useUnsyncedPwm = false;

static FAST_DATA_ZERO_INIT motorDevice_t motorPwmDevice;

static void pwmWriteUnused(uint8_t index, uint8_t mode, float value)
{
    UNUSED(index);
    UNUSED(mode);
    UNUSED(value);
}

void pwmShutdownPulsesForAllMotors(void)
{
    for (int index = 0; index < motorPwmDevice.count; index++) {
        picoPwmMotors[index].level = 0;
        pwm_set_chan_level(picoPwmMotors[index].slice, picoPwmMotors[index].channel, 0);
    }
}

void pwmDisableMotors(void)
{
    pwmShutdownPulsesForAllMotors();
}

static float pwmConvertToInternal(uint8_t index, uint8_t mode, float throttle)
{
    UNUSED(index);

    float value = motorConfig()->mincommand;

    if (mode == MOTOR_CONTROL_BIDIR) {
        if (throttle != 0)
            value = scaleRangef(throttle, -1, 1, motorConfig()->minthrottle, motorConfig()->maxthrottle);
    }
    else {
        if (throttle > 0)
            value = scaleRangef(throttle, 0, 1, motorConfig()->minthrottle, motorConfig()->maxthrottle);
    }

    return value;
}

static void pwmWriteStandard(uint8_t index, uint8_t mode, float throttle)
{
    const float value = pwmConvertToInternal(index, mode, throttle);
    const uint32_t level = constrain(lrintf(value * motors[index].pulseScale + motors[index].pulseOffset), 0, LEVELMAX);
    if (useUnsyncedPwm) {
        // Writes on a running slice latch at the next wrap (no glitches).
        pwm_set_chan_level(picoPwmMotors[index].slice, picoPwmMotors[index].channel, level);
    } else {
        // One-shot modes: The active compare level is typically 0 (between pulses) at this point.
        // Store the level to write it directly (unbuffered) while pwm stopped in pwmCompleteOneshotMotorUpdate.
        picoPwmMotors[index].level = level;
    }
}

static void pwmCompleteOneshotMotorUpdate(void)
{
    // One-shot modes: emit exactly one pulse per motor per update. Writes on a
    // running slice only latch at counter wrap, but are immediate when the counter is stopped.
    // So, write the levels with the slice stopped, zero the counter, then queue level 0 to latch
    // at the wrap (after the pulse ends).
    // Slices (which share counters across channels) are handled once each.
    for (int index = 0; index < motorPwmDevice.count; index++) {
        if (!picoPwmMotors[index].sliceHead) {
            continue;
        }
        const uint16_t slice = picoPwmMotors[index].slice;

        pwm_set_enabled(slice, false);
        for (int i = index; i < motorPwmDevice.count; i++) {
            if (picoPwmMotors[i].slice == slice) {
                pwm_set_chan_level(slice, picoPwmMotors[i].channel, picoPwmMotors[i].level);
            }
        }
        pwm_set_counter(slice, 0);
        pwm_set_enabled(slice, true);

        for (int i = index; i < motorPwmDevice.count; i++) {
            if (picoPwmMotors[i].slice == slice) {
                pwm_set_chan_level(slice, picoPwmMotors[i].channel, 0);
            }
        }
    }
}

bool pwmEnableMotors(void)
{
    /* check motors can be enabled */
    if (motorPwmDevice.vTable.write == &pwmWriteUnused) {
        return false;
    }

    // Re-route each motor pin back to the PWM slice. Matters after an ESC
    // 4-way session (io/serial_4way.c), which switches motor pins to plain
    // SIO GPIO for bit-banging; its esc4wayDeinit()'s IOCFG_AF_PP can't
    // restore a pico function mux. Idempotent when already routed.
    for (int index = 0; index < motorPwmDevice.count; index++) {
        if (picoPwmMotors[index].initialised) {
            gpio_set_function(IO_Pin(motors[index].io), GPIO_FUNC_PWM);
        }
    }

    return true;
}

bool pwmIsMotorEnabled(uint8_t index)
{
    return motors[index].enabled;
}

static motorVTable_t motorPwmVTable;

motorDevice_t *motorPwmDevInit(const motorDevConfig_t *motorDevConfig, uint8_t motorCount)
{
    memset(motors, 0, sizeof(motors));
    memset(picoPwmMotors, 0, sizeof(picoPwmMotors));

    useUnsyncedPwm = motorDevConfig->useUnsyncedPwm;

    float sMin = 0;
    float sLen = 0;
    switch (motorDevConfig->motorPwmProtocol) {
    default:
    case PWM_TYPE_ONESHOT125:
        sMin = 125e-6f;
        sLen = 125e-6f;
        break;
    case PWM_TYPE_ONESHOT42:
        sMin = 42e-6f;
        sLen = 42e-6f;
        break;
    case PWM_TYPE_MULTISHOT:
        sMin = 5e-6f;
        sLen = 20e-6f;
        break;
    case PWM_TYPE_STANDARD:
        sMin = 1e-3f;
        sLen = 1e-3f;
        useUnsyncedPwm = true;
        break;
    }

    motorPwmVTable.postInit = motorPostInitNull;
    motorPwmVTable.enable = pwmEnableMotors;
    motorPwmVTable.disable = pwmDisableMotors;
    motorPwmVTable.shutdown = pwmShutdownPulsesForAllMotors;
    motorPwmVTable.updateStart = motorUpdateStartNull;
    motorPwmVTable.write = pwmWriteStandard;
    motorPwmVTable.isMotorEnabled = pwmIsMotorEnabled;
    motorPwmVTable.updateComplete = useUnsyncedPwm ? motorUpdateCompleteNull : pwmCompleteOneshotMotorUpdate;

    motorPwmDevice.vTable = motorPwmVTable;

    for (int motorIndex = 0; motorIndex < MAX_SUPPORTED_MOTORS && motorIndex < motorCount; motorIndex++) {

        const ioTag_t tag = motorDevConfig->ioTags[motorIndex];

        motors[motorIndex].io = IOGetByTag(tag);
        if (!tag || !motors[motorIndex].io) {
            /* not enough motors initialised for the mixer or a break in the motors */
            motorPwmDevice.vTable.write = &pwmWriteUnused;
            motorPwmDevice.vTable.updateComplete = motorUpdateCompleteNull;
            /* TODO: block arming and add reason system cannot arm */
            return NULL;
        }

        const uint8_t pin = IO_PINBYTAG(tag);

        const uint16_t slice = pwm_gpio_to_slice_num(pin);
        const uint16_t channel = pwm_gpio_to_channel(pin);

        IOInit(motors[motorIndex].io, OWNER_MOTOR, RESOURCE_INDEX(motorIndex));

        picoPwmMotors[motorIndex].slice = slice;
        picoPwmMotors[motorIndex].channel = channel;

        bool sliceAlreadyUsed = false;
        for (int i = 0; i < motorIndex; i++) {
            if (picoPwmMotors[i].slice == slice) {
                sliceAlreadyUsed = true;
                break;
            }
        }
        picoPwmMotors[motorIndex].sliceHead = !sliceAlreadyUsed;

        /* standard PWM outputs */
        // margin of safety is 4 periods when not continuous
        const unsigned pwmRateHz = useUnsyncedPwm ? motorDevConfig->motorPwmRate : ceilf(1 / ((sMin + sLen) * 4));

        /*
            PWM Frequency = clock / (clkdiv * (wrap + 1))

            wrap is the 16-bit counter top (duty-cycle resolution); clkdiv is the
            pre-divider. To maximise resolution, use the smallest clkdiv that still
            lets a full period fit inside the 16-bit wrap register, then let wrap
            take up whatever's left.
        */
        const uint32_t clock = SystemCoreClock; // PICO timer clock is the CPU clock.

        // Clock ticks needed for one period
        const float ticksPerPeriod = (float)clock / (float)pwmRateHz;

        uint32_t clkdiv = (uint32_t)ceilf(ticksPerPeriod / 0xffff);
        clkdiv = constrain(clkdiv, 1, 255); // (Extra safety, but clkdiv isn't exceeding 256, even if sys clock was high as 800MHz, pwm rate as low as 50Hz)

        const uint32_t hz = clock / clkdiv; // counter tick rate after the divider

        int32_t wrap;
        if (useUnsyncedPwm) {
            int32_t period = lrintf(ticksPerPeriod / (float)clkdiv);
            wrap = constrain(period - 1, 0, TOPMAX);
        } else {
            wrap = TOPMAX;
        }

        pwm_config config = pwm_get_default_config();

        pwm_config_set_clkdiv_int(&config, clkdiv);
        pwm_config_set_wrap(&config, wrap);
        gpio_set_function(pin, GPIO_FUNC_PWM);

        pwm_set_chan_level(slice, channel, 0);
        pwm_init(slice, &config, true);

        motors[motorIndex].pulseScale = (sLen * hz) / 1000.0f;
        motors[motorIndex].pulseOffset = (sMin * hz) - (motors[motorIndex].pulseScale * 1000);
        motors[motorIndex].enabled = true;
        picoPwmMotors[motorIndex].initialised = true;
    }

    return &motorPwmDevice;
}

pwmOutputPort_t *pwmGetMotors(void)
{
    return motors;
}

#endif // USE_PWM_OUTPUT
