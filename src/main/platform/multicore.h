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

// Ported from betaflight's src/platform/PICO/include/platform/multicore.h.
// Declares the small command-dispatch API implemented in drivers/multicore.c,
// used to run functions on RP2350/RP2354's second core (USE_MULTICORE).

#pragma once

#include "pico/multicore.h"

typedef enum multicoreCommand_e {
    MULTICORE_CMD_NONE = 0,
    MULTICORE_CMD_FUNC,
    MULTICORE_CMD_FUNC_BLOCKING, // Command to execute a function on the second core and wait for completion
    MULTICORE_CMD_STOP, // Command to stop the second core
} multicoreCommand_e;

// Define function types for clarity
typedef void core1_func_t(void);

void multicoreStart(void);
void multicoreStop(void);
void multicoreExecute(core1_func_t *func);
void multicoreExecuteBlocking(core1_func_t *func);

// Core-1 task consumer registration (docs/RP2350-Porting-Plan.md's "Core-1
// task consumer design"). A consumer is a non-blocking, bounded drain
// function core 1's main loop calls once per pass, round-robin with every
// other registered consumer and the RPC command queue above - so no single
// consumer can hog core 1 and starve the others. ctx is passed back
// verbatim (typically a pointer to the consumer's own ring buffer/state);
// drainFn must not block (no queue_*_blocking, no long loops) since it runs
// inline in core 1's loop.
typedef void (multicoreConsumerDrainFn_t)(void *ctx);
bool multicoreRegisterConsumer(multicoreConsumerDrainFn_t *drainFn, void *ctx);

// Peripheral interrupt affinity. Each Cortex-M33 has its own NVIC, so
// irq_set_enabled() only ever unmasks an interrupt on the core that calls it -
// a handler registered during core-0 peripheral init will never fire on core 1
// no matter what the vector table says. Register the IRQ here instead and
// core 1 unmasks it for itself; calls made before core 1 launches are applied
// at startup, later ones on its next loop pass. The handler itself must still
// be installed the usual way (irq_set_exclusive_handler/irq_add_shared_handler),
// which writes the single shared vector table and can be done from either core.
//
// Anything driven by such an interrupt then belongs to core 1 exclusively:
// SDK code that masks an interrupt for critical sections (TinyUSB's
// dcd_int_disable(), for one) only masks it on its own core, so core 0 must
// not also call into that peripheral's stack.
bool multicoreEnableIrqOnCore1(uint irqNum);

// Incremented once per core-1 main-loop pass. Core 0 can poll this (e.g.
// once a second) to detect a wedged core 1 (per the design's "failure
// isolation" requirement) and degrade gracefully without affecting flight
// control - core 0 must never block waiting on core 1.
uint32_t multicoreGetHeartbeat(void);
