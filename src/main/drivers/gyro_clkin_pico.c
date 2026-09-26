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

#include "platform.h"

#if defined(USE_GYRO_CLK)

#include <stdbool.h>
#include <stdint.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "drivers/accgyro/accgyro.h"
#include "drivers/accgyro/gyro_sync.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/resource.h"

#include "pg/gyrodev.h"

#include "sensors/gyro.h"

// PICO version of gyroExternalClockInit() (the STM32 one in gyro_sync.c
// allocates a hardware timer): a 50% square wave at clockFreq from the PWM
// slice behind the gyro's CLKIN pin (resource GYRO_CLK).

static IO_t gyroClkIO = IO_NONE;

bool gyroExternalClockInit(const extDevice_t *dev, uint32_t clockFreq)
{
    const int cfg = 0; // Only on 1st gyro

    if (&gyro.gyroSensor1.gyroDev.dev != dev) {
        return false;
    }

    const IO_t io = IOGetByTag(gyroDeviceConfig(cfg)->clkInTag);
    if (!io || clockFreq == 0) {
        return false;
    }

    if (gyroClkIO) {
        // already running, OK if the same pin is shared
        return gyroClkIO == io;
    }

    const uint32_t pin = IO_Pin(io);
    const uint32_t slice = pwm_gpio_to_slice_num(pin);
    const uint32_t channel = pwm_gpio_to_channel(pin);

    // clkdiv 1, so the 16-bit wrap sets the lower limit (sys_clk / 65536,
    // ~2.3kHz at 150MHz) - well below the 32kHz CLKIN rate.
    const uint32_t divisor = clock_get_hz(clk_sys) / clockFreq;
    if (divisor < 2 || divisor > (uint32_t)UINT16_MAX + 1) {
        return false;
    }

    // Both channels of a slice share clkdiv/wrap: refuse a slice something
    // else (motor, servo, beeper) already runs. Gyro init runs before
    // motor/servo init, so they find this slice taken in turn.
    if (pwm_hw->en & (1u << slice)) {
        return false;
    }

    const uint16_t wrap = (uint16_t)(divisor - 1);

    IOInit(io, OWNER_GYRO_CLK, RESOURCE_INDEX(cfg));
    gpio_set_function(pin, GPIO_FUNC_PWM);
    pwm_set_clkdiv_int_frac(slice, 1, 0);
    pwm_set_wrap(slice, wrap);
    pwm_set_chan_level(slice, channel, (uint16_t)((wrap + 1u) / 2u));
    pwm_set_enabled(slice, true);

    gyroClkIO = io;

    return true;
}

#endif
