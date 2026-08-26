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

#pragma once

#include "build/debug.h"
#include "build/debug_pin.h"

#include "drivers/io.h"
#include "drivers/dshot.h"
#include "drivers/dshot_command.h"
// NB: upstream Betaflight (see betaflight/src/platform/PICO/dshot_pico.h)
// includes "drivers/motor_types.h" and uses its motorProtocolTypes_e /
// motorVTable_t (postInit/telemetryWait/decodeTelemetry/updateInit/isMotorIdle/
// requestTelemetry/convertExternalToMotor/convertMotorToExternal). Wingflight
// forked before that motor-API refactor and still has the older, simpler
// drivers/motor.h contract (motorPwmProtocolTypes_e, and a motorVTable_t with
// just postInit/enable/disable/shutdown/updateStart/updateComplete/write(index,
// mode,value)/writeInt/isMotorEnabled - see drivers/motor.c, dshot_bitbang.c,
// pwm_output_dshot_shared.c for the real, working contract). dshot_pico.c and
// dshot_bidir_pico.c are adapted to that older contract.
#include "drivers/motor.h"
// For motorDmaOutput_t/getMotorDmaOutput() - the cross-backend "shared
// per-motor DSHOT command state" contract that dshot_command.c's
// allMotorsAreIdle()/dshotCommandWrite() use unconditionally (regardless of
// which backend is active). dshot_dpwm.c itself (the STM32 DMA backend that
// normally provides the getMotorDmaOutput() definition) is excluded for PICO
// via MCU_EXCLUDES - dshot_pico.c provides its own definition instead.
#include "drivers/dshot_dpwm.h"
#include "drivers/time.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "dshot_pio_programs.h"

#define PIO_SHIFT_LEFT (false)
#define PIO_SHIFT_RIGHT (true)
#define PIO_NO_AUTO_PUSHPULL (false)

typedef struct motorOutput_s {
    int offset;              // NB current code => offset same for all motors
    dshotProtocolControl_t protocolControl;
    int pinIndex;            // pinIndex of this motor output
    IO_t io;                 // IO_t for this output
    bool configured;
    bool enabled;
    PIO pio;
    uint16_t pio_sm;
} motorOutput_t;

extern const PIO dshotPio; // currently only single pio supported => 4 motors.

extern motorPwmProtocolTypes_e dshotMotorProtocol;

extern motorOutput_t dshotMotors[MAX_SUPPORTED_MOTORS];

// Number of motors actually configured by dshotPwmDevInit() - not present in
// Wingflight's dshot.h (unlike upstream Betaflight's), so declared here since
// only the PICO PIO backend needs it shared between dshot_pico.c and
// dshot_bidir_pico.c.
extern uint8_t dshotMotorCount;

float dshotGetPeriodTiming(void);

bool dshot_program_bidir_init(PIO pio, uint sm, int offset, uint pin);
bool dshotDecodeTelemetry(void);
