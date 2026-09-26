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

/*
 * Castle Link Live telemetry for PICO - the RP2350 counterpart of
 * drivers/castle_telemetry_decode.c.
 *
 * Castle uses an active-low throttle pulse on an open-drain line with a
 * pull-up. Between pulses the ESC briefly pulls the line low (a "tick"); the
 * delay from the end of the pulse to the tick is the telemetry value. A
 * frame without a tick is the sync frame that starts the next set of
 * CASTLE_TELEM_NFRAMES values.
 *
 * The STM32 driver flips one timer channel between PWM output and input
 * capture. RP2350 PWM slices can neither release the line nor capture, so a
 * PIO state machine does the whole frame: drive the pulse (pindirs, so the
 * line is only ever pulled low), release, time the tick, push the result and
 * pad the frame to the configured period. It runs at 2MHz with 2 cycles per
 * loop, so every count is 1us.
 *
 * The CPU side exchanges one word per frame from the motor write, which runs
 * every PID loop - far more often than the 50-100Hz frame rate, so the
 * 4-deep RX FIFO never fills.
 *
 * Only one Castle ESC (the first motor) reports telemetry, as on STM32.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#if defined(USE_TELEMETRY_CASTLE) && defined(USE_PWM_OUTPUT)

#include "common/maths.h"
#include "common/utils.h"

#include "drivers/castle_telemetry_decode.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"

#include "hardware/gpio.h"
#include "hardware/pio.h"

// PIO block shared with the LED strip (one SM each)
#define CASTLE_PIO_INDEX PIO_LEDSTRIP_INDEX

#define CASTLE_SM_HZ 2000000 // 2 cycles per loop -> 1us per count

// Cycles between releasing the line and the first sample (set [7] + out),
// in us, added back to every tick delay.
#define CASTLE_SETTLE_US 5

// Per-frame fixed cost of the program outside the timed loops, in us
#define CASTLE_FRAME_OVERHEAD_US 10

// No tick within the window: the low 16 bits of y are all ones
#define CASTLE_NO_TICK 0xFFFF

/*
 * Frame word (pushed by the CPU, reused by `pull noblock` until replaced):
 *   bits 31..16  pulse length - 1, us
 *   bits 15..0   window after the pulse, us (tick search + padding)
 * Result word (pushed by the SM once per frame):
 *   bits 31..16  window of that frame
 *   bits 15..0   window count left when the tick was seen, or 0xFFFF if none
 * OSR and ISR both shift left (MSB first), no auto pull/push.
 */
static const uint16_t castle_program_instructions[] = {
            //     .wrap_target
    0x8080, //  0: pull   noblock                      ; new frame word, or X again
    0xa027, //  1: mov    x, osr                       ; keep it for the next frame
    0x6050, //  2: out    y, 16                        ; pulse length - 1
    0xe081, //  3: set    pindirs, 1                   ; drive low
    0x0184, //  4: jmp    y--, 4                 [1]   ; 1us per loop
    0xe780, //  5: set    pindirs, 0             [7]   ; release, let the pull-up settle
    0x6050, //  6: out    y, 16                        ; window
    0x00c9, //  7: jmp    pin, 9                       ; still high?
    0x000e, //  8: jmp    14                           ; no: tick
    0x0087, //  9: jmp    y--, 7                       ; 1us per loop
    0x4030, // 10: in     x, 16                        ; window over, no tick
    0x4050, // 11: in     y, 16                        ; y = 0xffffffff
    0x8000, // 12: push   noblock
    0x0000, // 13: jmp    0
    0x4030, // 14: in     x, 16                        ; tick
    0x4050, // 15: in     y, 16
    0x8000, // 16: push   noblock
    0x0191, // 17: jmp    y--, 17                [1]   ; pad the rest of the window
            //     .wrap
};

static const struct pio_program castle_program = {
    .instructions = castle_program_instructions,
    .length = ARRAYLEN(castle_program_instructions),
    .origin = -1,
    .pio_version = 0,
    .used_gpio_ranges = 0x0,
};

typedef struct castleState_s {
    PIO pio;
    int sm;
    int offset;
    uint pin;
    uint32_t periodUs;
    uint16_t pulseUs;
    bool running;
    castleTelemetry_t telem[2];
    uint8_t whichTelem; // telem being written (0 or 1)
    uint8_t telemIndex; // where in the telem struct we are
} castleState_t;

static castleState_t castleState = { .sm = -1 };

void getCastleTelemetry(castleTelemetry_t* telem)
{
    // Written by castlePicoWrite() in the motor update, read by the ESC
    // sensor task - both on core 0 in thread context, so no locking.
    memcpy(telem, &castleState.telem[castleState.whichTelem ^ 1], sizeof(castleTelemetry_t));
}

// Same frame bookkeeping as the STM32 pwmEdgeCallback()
static void castleProcessFrame(uint32_t result)
{
    castleState_t *state = &castleState;

    const uint16_t window = result >> 16;
    const uint16_t left = result & 0xFFFF;

    if (left == CASTLE_NO_TICK) {
        // No tick: sync frame
        state->telemIndex = 1;
    } else if (state->telemIndex > 0) {
        const uint16_t telemVal = window - left + CASTLE_SETTLE_US;
        ((uint16_t*)&state->telem[state->whichTelem])[state->telemIndex] = telemVal;
        if (telemVal <= (state->pulseUs >> 4)) {
            // When the battery is disconnected we get some spurious
            // telemetry frames.
            state->telemIndex = 0;
        } else if (++state->telemIndex == CASTLE_TELEM_NFRAMES) {
            state->telemIndex = 0;
            // Note the first valid telemetry generation is 1.
            state->telem[state->whichTelem ^ 1].generation =
                ++state->telem[state->whichTelem].generation;
            state->whichTelem ^= 1;
        }
    }
}

static uint32_t castleFrameWord(uint16_t pulseUs)
{
    const uint32_t window = castleState.periodUs - pulseUs - CASTLE_FRAME_OVERHEAD_US;
    return ((uint32_t)(pulseUs - 1) << 16) | (window & 0xFFFF);
}

bool castlePicoInit(IO_t io, uint16_t rateHz)
{
    if (castleState.sm >= 0 || !io || rateHz < CASTLE_PWM_HZ_MIN || rateHz > CASTLE_PWM_HZ_MAX) {
        return false;
    }

    const PIO pio = PIO_INSTANCE(CASTLE_PIO_INDEX);
    const uint pin = IO_Pin(io);

    // The block addresses a 32-pin window; pins >= 32 (RP2350B) need base 16
    const uint base = pio_get_gpio_base(pio);
    if (pin < base || pin >= base + 32) {
        if (pin >= 32 && base == 0 && pio_set_gpio_base(pio, 16) == PICO_OK) {
            // moved the (still unused) window up
        } else {
            return false;
        }
    }

    if (!pio_can_add_program(pio, &castle_program)) {
        return false;
    }
    const int sm = pio_claim_unused_sm(pio, false);
    if (sm < 0) {
        return false;
    }
    const int offset = pio_add_program(pio, &castle_program);

    castleState.pio = pio;
    castleState.sm = sm;
    castleState.offset = offset;
    castleState.pin = pin;
    castleState.periodUs = 1000000 / rateHz;
    castleState.pulseUs = 1000;

    // Open drain: the output value stays 0, only the direction is switched.
    // The line needs a pull-up; the internal one is enabled, a Castle ESC
    // may need a stronger external one.
    pio_gpio_init(pio, pin);
    gpio_set_pulls(pin, true, false);
    pio_sm_set_pins_with_mask64(pio, sm, 0, 1ull << pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, false);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + castle_program.length - 1);
    sm_config_set_set_pins(&c, pin, 1);
    sm_config_set_jmp_pin(&c, pin);
    sm_config_set_in_pins(&c, pin);
    sm_config_set_out_shift(&c, false, false, 32);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_clkdiv(&c, (float)SystemCoreClock / CASTLE_SM_HZ);

    if (pio_sm_init(pio, sm, offset, &c) != PICO_OK) {
        pio_sm_unclaim(pio, sm);
        pio_remove_program(pio, &castle_program, offset);
        castleState.sm = -1;
        return false;
    }

    return true;
}

// Start/stop the pulse train. Stopped, the line is released (idle high).
static void castleSetRunning(bool run)
{
    castleState_t *state = &castleState;

    if (run == state->running) {
        return;
    }

    if (run) {
        pio_sm_clear_fifos(state->pio, state->sm);
        pio_sm_restart(state->pio, state->sm);
        pio_sm_exec(state->pio, state->sm, pio_encode_jmp(state->offset));
        pio_sm_put(state->pio, state->sm, castleFrameWord(state->pulseUs));
        pio_sm_set_enabled(state->pio, state->sm, true);
    } else {
        pio_sm_set_enabled(state->pio, state->sm, false);
        pio_sm_set_consecutive_pindirs(state->pio, state->sm, state->pin, 1, false);
        state->telemIndex = 0;
    }
    state->running = run;
}

// Motor output: pulse length in us, 0 stops the pulses
void castlePicoWrite(uint16_t pulseUs)
{
    castleState_t *state = &castleState;

    if (state->sm < 0) {
        return;
    }

    while (!pio_sm_is_rx_fifo_empty(state->pio, state->sm)) {
        castleProcessFrame(pio_sm_get(state->pio, state->sm));
    }

    if (pulseUs == 0) {
        castleSetRunning(false);
        return;
    }

    state->pulseUs = constrain(pulseUs, 1, state->periodUs / 2);

    if (!state->running) {
        castleSetRunning(true);
    } else if (pio_sm_is_tx_fifo_empty(state->pio, state->sm)) {
        // Taken at the start of the next frame
        pio_sm_put(state->pio, state->sm, castleFrameWord(state->pulseUs));
    }
}

// Re-route the pin to the PIO block (after an ESC 4-way session left it on SIO)
void castlePicoEnable(void)
{
    if (castleState.sm >= 0) {
        pio_gpio_init(castleState.pio, castleState.pin);
    }
}

#endif
