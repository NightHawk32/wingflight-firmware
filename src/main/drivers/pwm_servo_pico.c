/*
 * This file is part of Wingflight.
 *
 * Wingflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Wingflight is distributed in the hope that it will be useful,
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

#if defined(PICO) && defined(USE_SERVOS)

#include "drivers/pwm_servo_pico.h"

#include "common/maths.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/resource.h"

#include "flight/servos.h"
#include "pg/servos.h"

#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "platform/pwm.h"

// Same clamp used by pwm_motor_pico.c's standard-PWM path: maximum practical
// TOP/compare value, safely inside the 16-bit PWM counter regardless of the
// configured wrap for a given servo's rate.
#define TOPMAX (0xfffe)

static picoPwmOutput_t picoPwmServos[MAX_SUPPORTED_SERVOS];
// Ticks-per-microsecond at each servo's own PWM slice clock, used to convert
// a microsecond pulse width (servoSetOutput()'s 'pos') into a compare level.
// Equivalent to servoResolution[] in the STM32 timerChannel_t path.
static float servoTicksPerUs[MAX_SUPPORTED_SERVOS];

uint8_t picoServoDevInit(const ioTag_t *ioTags)
{
    memset(picoPwmServos, 0, sizeof(picoPwmServos));
    memset(servoTicksPerUs, 0, sizeof(servoTicksPerUs));

    uint8_t index;
    for (index = 0; index < MAX_SUPPORTED_SERVOS && ioTags[index]; index++) {
        const ioTag_t tag = ioTags[index];
        const IO_t io = IOGetByTag(tag);
        if (!io) {
            break;
        }

        const uint8_t pin = IO_PINBYTAG(tag);
        const uint16_t slice = pwm_gpio_to_slice_num(pin);
        const uint16_t channel = pwm_gpio_to_channel(pin);

        IOInit(io, OWNER_SERVO, RESOURCE_INDEX(index));

        picoPwmServos[index].slice = slice;
        picoPwmServos[index].channel = channel;

        bool sliceAlreadyUsed = false;
        for (int i = 0; i < index; i++) {
            if (picoPwmServos[i].slice == slice) {
                sliceAlreadyUsed = true;
                break;
            }
        }
        picoPwmServos[index].sliceHead = !sliceAlreadyUsed;

        // PWM Frequency = clock / (clkdiv * (wrap + 1)); use the smallest
        // clkdiv that still fits a full period into the 16-bit wrap
        // register, then let wrap take up the remaining resolution (same
        // approach as pwm_motor_pico.c's standard-PWM path).
        const unsigned pwmRateHz = constrain(servoParams(index)->rate, SERVO_RATE_MIN, SERVO_RATE_MAX);
        const uint32_t clock = SystemCoreClock; // PICO PWM clock is the CPU clock.
        const float ticksPerPeriod = (float)clock / (float)pwmRateHz;

        uint32_t clkdiv = (uint32_t)ceilf(ticksPerPeriod / 0xffff);
        clkdiv = constrain(clkdiv, 1, 255);

        const uint32_t hz = clock / clkdiv; // counter tick rate after the divider
        const int32_t period = lrintf(ticksPerPeriod / (float)clkdiv);
        const int32_t wrap = constrain(period - 1, 0, TOPMAX);

        pwm_config config = pwm_get_default_config();
        pwm_config_set_clkdiv_int(&config, clkdiv);
        pwm_config_set_wrap(&config, wrap);
        gpio_set_function(pin, GPIO_FUNC_PWM);

        servoTicksPerUs[index] = (float)hz / 1e6f;

        // Start at the configured center pulse so the servo doesn't jump on
        // the first real update.
        const uint32_t initialLevel = constrain(lrintf(servoParams(index)->mid * servoTicksPerUs[index]), 0, TOPMAX);
        pwm_set_chan_level(slice, channel, initialLevel);
        pwm_init(slice, &config, true);

        picoPwmServos[index].initialised = true;
    }

    return index;
}

void picoServoWrite(uint8_t index, float pos)
{
    if (index >= MAX_SUPPORTED_SERVOS || !picoPwmServos[index].initialised) {
        return;
    }

    const uint32_t level = constrain(lrintf(pos * servoTicksPerUs[index]), 0, TOPMAX);
    pwm_set_chan_level(picoPwmServos[index].slice, picoPwmServos[index].channel, level);
}

void picoServoShutdown(void)
{
    for (int index = 0; index < MAX_SUPPORTED_SERVOS; index++) {
        if (picoPwmServos[index].initialised) {
            pwm_set_chan_level(picoPwmServos[index].slice, picoPwmServos[index].channel, 0);
        }
    }
}

#endif // defined(PICO) && defined(USE_SERVOS)
