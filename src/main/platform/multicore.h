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
//
// Return true if this call moved data, meaning there may be more to do right
// now; core 1 then goes straight round again. Return false when the consumer
// is idle or is waiting on something that will interrupt (a USB endpoint the
// host has yet to drain, say) - once every consumer says false, core 1 sleeps
// in WFE until an interrupt or multicoreSignalWork() wakes it. A consumer that
// always returns true turns core 1 back into a spin loop.
typedef bool (multicoreConsumerDrainFn_t)(void *ctx);
bool multicoreRegisterConsumer(multicoreConsumerDrainFn_t *drainFn, void *ctx);

// Wake core 1 if it is sleeping. Core 0 must call this after making work
// visible to a consumer (queueing bytes, freeing space a consumer is blocked
// on), or core 1 may sleep through it. Cheap - a single SEV instruction - and
// safe to call redundantly: the event flag is sticky, so a signal that lands
// before core 1 reaches WFE simply makes that WFE return at once, and there is
// no lost-wakeup window to reason about.
void multicoreSignalWork(void);

// Core-0-only liveness probe, and the latched verdict.
//
// Core 0 must never depend on a core-1 result for flight (see the porting
// plan's "failure isolation"), which means noticing when core 1 stops. Call
// multicoreCheckCore1Alive() periodically from any core-0 context: it wakes
// core 1 and checks the loop counter moved since the previous call, latching
// false after several consecutive misses. Since core 1 sleeps when idle, the
// probe is what guarantees it wakes at all - a healthy but idle core 1 would
// otherwise look identical to a wedged one.
//
// Latched, never re-armed: a core 1 that has stopped once is not trusted
// again until reboot. Callers are expected to stop handing it work and stop
// waiting on it, not to try to revive it.
bool multicoreCheckCore1Alive(void);
bool multicoreIsCore1Alive(void);

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
