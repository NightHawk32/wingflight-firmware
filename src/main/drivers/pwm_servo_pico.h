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

#pragma once

// PICO standard-PWM servo output, driven directly via RP2 PWM slices (there
// is no timerHardware_t/timerAllocate on PICO, unlike the STM32 timer path
// in flight/servos.c). Called only from flight/servos.c under #if
// defined(PICO); see pwm_servo_pico.c for the implementation.

#include "drivers/io_types.h"

// Configures one PWM slice/channel per servo pin listed in ioTags (stopping
// at the first IO_TAG_NONE entry, same convention as the STM32 path), each
// at its own configured servoParams()->rate. Returns the number of servos
// successfully configured (the new servoCount).
uint8_t picoServoDevInit(const ioTag_t *ioTags);

// pos is a pulse width in microseconds (same convention as the STM32 path's
// *ccr = pos * servoResolution write in servoSetOutput()).
void picoServoWrite(uint8_t index, float pos);

void picoServoShutdown(void);
