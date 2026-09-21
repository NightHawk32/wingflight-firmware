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
#include "common/utils.h"
#include "drivers/time.h"
#include "platform/multicore.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/flash.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#ifdef USE_MULTICORE

// Define a structure for the message we'll pass through the queue
typedef struct {
    multicoreCommand_e command;
    core1_func_t *func;
} core_message_t;

// Define the queue
static queue_t core0_queue;
static queue_t core1_queue;

// Round-robin work-buffer consumers - see multicoreRegisterConsumer().
#define MAX_MULTICORE_CONSUMERS 4

typedef struct {
    multicoreConsumerDrainFn_t *drainFn;
    void *ctx;
} multicoreConsumerEntry_t;

static multicoreConsumerEntry_t multicoreConsumers[MAX_MULTICORE_CONSUMERS];
static volatile uint8_t multicoreConsumerCount;

static volatile uint32_t multicoreHeartbeat;

// --- core-1 liveness, as seen from core 0 (multicoreCheckCore1Alive()) ---
#define MULTICORE_HEALTH_INTERVAL_MS 100
#define MULTICORE_HEALTH_STRIKES     3 // ~300ms before core 1 is written off

static bool multicoreCore1Alive = true;
static uint32_t multicoreCore1SeenHeartbeat;
static timeMs_t multicoreCore1LastCheckMs;
static uint8_t multicoreCore1Strikes;

// Cross-core wake. SEV is not usable for this: the SDK's own lock primitives
// execute SEV on release, so core 1 signals itself several times per pass and
// a WFE would never actually wait (measured: 566k passes a second, higher than
// the plain spin it replaced). WFI ignores events, and RP2350's doorbells are
// the matching wake - a latching, per-core interrupt one core can raise on the
// other. Latching matters: a doorbell rung before core 1 has unmasked the IRQ
// is still delivered when it does.
static int multicoreDoorbell = -1;

static void multicoreDoorbellHandler(void)
{
    // Clearing is all it has to do; waking core 1 out of WFI is the point.
    multicore_doorbell_clear_current_core(multicoreDoorbell);
}

void multicoreSignalWork(void)
{
    if (multicoreDoorbell >= 0) {
        multicore_doorbell_set_other_core(multicoreDoorbell);
    }
}

bool multicoreIsCore1Alive(void)
{
    return multicoreCore1Alive;
}

bool multicoreCheckCore1Alive(void)
{
    if (!multicoreCore1Alive) {
        return false;
    }

    const timeMs_t nowMs = millis();
    if (multicoreCore1LastCheckMs && (int32_t)(nowMs - multicoreCore1LastCheckMs) < MULTICORE_HEALTH_INTERVAL_MS) {
        return true; // not due yet
    }
    multicoreCore1LastCheckMs = nowMs ? nowMs : 1; // 0 means "never checked"

    const uint32_t heartbeat = multicoreHeartbeat;
    if (heartbeat != multicoreCore1SeenHeartbeat) {
        multicoreCore1SeenHeartbeat = heartbeat;
        multicoreCore1Strikes = 0;
    } else if (++multicoreCore1Strikes >= MULTICORE_HEALTH_STRIKES) {
        // Core 1 has not been round its loop once in three intervals despite
        // being woken each time. It is not coming back.
        multicoreCore1Alive = false;
    }

    // Wake it, so an idle-but-healthy core 1 has moved the counter on by the
    // next check. Without this the probe could not tell asleep from dead.
    multicoreSignalWork();

    return multicoreCore1Alive;
}

// IRQs core 1 must unmask on its own NVIC - see multicoreEnableIrqOnCore1().
#define MAX_MULTICORE_CORE1_IRQS 8

static volatile uint8_t multicoreCore1Irqs[MAX_MULTICORE_CORE1_IRQS];
static volatile uint8_t multicoreCore1IrqCount;
static uint8_t multicoreCore1IrqsApplied;

bool multicoreEnableIrqOnCore1(uint irqNum)
{
    if (multicoreCore1IrqCount >= MAX_MULTICORE_CORE1_IRQS) {
        return false;
    }
    multicoreCore1Irqs[multicoreCore1IrqCount] = (uint8_t)irqNum;
    multicoreSignalWork();
    // Release fence, as in multicoreRegisterConsumer(): core 1 polls the
    // count, and must not see it before the entry it refers to.
    __atomic_thread_fence(__ATOMIC_RELEASE);
    multicoreCore1IrqCount++;
    return true;
}

// Core 1 only. Unmasks anything registered since the last pass.
static void multicoreApplyCore1Irqs(void)
{
    const uint8_t count = multicoreCore1IrqCount;
    if (count == multicoreCore1IrqsApplied) {
        return;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    while (multicoreCore1IrqsApplied < count) {
        irq_set_enabled(multicoreCore1Irqs[multicoreCore1IrqsApplied], true);
        multicoreCore1IrqsApplied++;
    }
}

bool multicoreRegisterConsumer(multicoreConsumerDrainFn_t *drainFn, void *ctx)
{
    if (!drainFn || multicoreConsumerCount >= MAX_MULTICORE_CONSUMERS) {
        return false;
    }
    multicoreConsumers[multicoreConsumerCount].drainFn = drainFn;
    multicoreConsumers[multicoreConsumerCount].ctx = ctx;
    multicoreSignalWork();
    // Release fence: core 1 polls multicoreConsumerCount from its loop, and
    // Armv8-M normal memory is weakly ordered - without this it could
    // observe the incremented count before the entry stores above and call
    // a garbage/NULL drainFn.
    __atomic_thread_fence(__ATOMIC_RELEASE);
    multicoreConsumerCount++;
    return true;
}

uint32_t multicoreGetHeartbeat(void)
{
    return multicoreHeartbeat;
}

static void core1_main(void)
{
    // Register this core as a flash_safe_execute() lockout victim so core 0
    // can park it (out of XIP) for the duration of a flash erase/program -
    // config_streamer.c's PICO flash writes go through flash_safe_execute(),
    // which returns PICO_ERROR_NOT_POSSIBLE once core 1 is launched unless
    // this has been called here. Without the lockout, core 1 fetching code
    // from flash (e.g. the cli/pg sections kept in flash by the RunFromHybrid
    // layout) mid-erase would bus-fault or hang.
    flash_safe_execute_core_init();

    // Each Cortex-M33 core has its own independent NVIC, so anything core 0
    // asked to have serviced here (USB, for one) has to be unmasked from here.
    multicoreApplyCore1Irqs();

    // This loop is run on the second core. For now the RPC consumer below IS
    // core 1's task loop; dedicated lock-free ring-buffer consumers for
    // latency-tolerant work (USB-MSC block I/O, blackbox flush, CLI/MSP
    // parsing) are designed in docs/RP2350-Porting-Plan.md's "Core-1 task
    // consumer design" and get added here as those producers materialise.
    while (true) {
        // Set by anything that did work this pass; while it stays false there
        // is nothing to come back for, and core 1 sleeps at the bottom of the
        // loop rather than spinning.
        bool busy = false;

        core_message_t msg;
        if (queue_try_remove(&core1_queue, &msg)) {
            busy = true;
            switch (msg.command) {
            case MULTICORE_CMD_FUNC:
                if (msg.func) {
                    msg.func();
                }
                break;
            case MULTICORE_CMD_FUNC_BLOCKING:
                if (msg.func) {
                    msg.func();

                    // Send the result back to core0 (it will be blocking until this is done)
                    bool result = true;
                    queue_add_blocking(&core0_queue, &result);
                }
                break;
            case MULTICORE_CMD_STOP: {
                // multicore_reset_core1() is a CORE-0-ONLY API (its first
                // action force-powers-off proc1, so executed here it kills
                // this core mid-function with the SDK's core-1 status and
                // handshake left inconsistent). Instead: deinit, ack core 0
                // (multicoreStop() blocks on this), and return - falling out
                // of core1_main() parks this core in the SDK's wait loop,
                // after which core 0 performs the actual reset.
                flash_safe_execute_core_deinit();
                bool stopped = true;
                queue_add_blocking(&core0_queue, &stopped);
                return; // Exit the core1_main function
            }
            default:
                // unknown command or none
                break;
            }
        }

        // Round-robin pump: one bounded drain call per registered consumer
        // per pass, so a busy consumer can't starve the others or the RPC
        // queue above. Each drainFn is responsible for its own bound (e.g.
        // "at most N bytes this call") - this loop does not enforce one.
        // Acquire fence pairs with multicoreRegisterConsumer()'s release:
        // once the count is observed, the entries behind it are visible.
        const uint8_t consumerCount = multicoreConsumerCount;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        for (uint8_t i = 0; i < consumerCount; i++) {
            if (multicoreConsumers[i].drainFn) {
                busy |= multicoreConsumers[i].drainFn(multicoreConsumers[i].ctx);
            }
        }

        multicoreApplyCore1Irqs();

        multicoreHeartbeat++;

        if (!busy && multicoreDoorbell >= 0) {
            // Idle: wait for an interrupt of our own (USB, mostly) or the
            // doorbell core 0 rings from multicoreSignalWork(). Spinning
            // instead would fetch instructions and data out of the same SRAM
            // the flight loop runs from - the whole image lives in RAM on this
            // port - and burn the power for it, hundreds of thousands of times
            // a second, to find there is nothing to do.
            //
            // Masking interrupts around the wait is what closes the race with
            // core 0: an interrupt that arrives from here on stays pending
            // instead of being taken and silently consumed, and a pending
            // interrupt still wakes WFI even with PRIMASK set. So work that
            // lands in this window is not slept through.
            const uint32_t irqState = save_and_disable_interrupts();
            __wfi();
            restore_interrupts(irqState);
        }
    }
}

void multicoreStart(void)
{
    // Claim the wake doorbell before launching, so core 1 has it unmasked on
    // its first pass and can never sleep unwakeably. Without one, core 1 keeps
    // spinning rather than risk sleeping through work (see core1_main()).
    multicoreDoorbell = multicore_doorbell_claim_unused(0x3, false);
    if (multicoreDoorbell >= 0) {
        irq_set_exclusive_handler(multicore_doorbell_irq_num(multicoreDoorbell), multicoreDoorbellHandler);
        multicoreEnableIrqOnCore1(multicore_doorbell_irq_num(multicoreDoorbell));
    }

    // Put core 1 back in the bootrom's wait loop before handing it a new
    // entry point. multicore_launch_core1()'s handshake talks to that wait
    // loop over the inter-core FIFO, and a core 1 already running
    // core1_main() never reads the FIFO - so the launch blocks forever.
    // That is not hypothetical: a reset that restarts only core 0 (a
    // debugger's SYSRESETREQ, or any reset path that does not go through
    // systemResetHard()) leaves core 1 running the previous image, and the
    // next boot then deadlocks in systemInit() before the flight code has
    // started. Resetting first is idempotent when core 1 was never launched.
    multicore_reset_core1();

    // Initialize the queue with a size of 4 (to be determined based on expected load)
    queue_init(&core1_queue, sizeof(core_message_t), 4);

    // Initialize the queue with a size of 1 (only needed for blocking results)
    queue_init(&core0_queue, sizeof(bool), 1);

    // Start core 1
    multicore_launch_core1(core1_main);
}

void multicoreStop(void)
{
    core_message_t msg;
    msg.command = MULTICORE_CMD_STOP;
    msg.func = NULL;

    queue_add_blocking(&core1_queue, &msg);
    multicoreSignalWork();

    // Wait for core 1 to acknowledge (deinit done, exiting its main loop),
    // then reset it from here - the only core allowed to (see the STOP
    // handler in core1_main()).
    bool stopped;
    queue_remove_blocking(&core0_queue, &stopped);
    multicore_reset_core1();
 }
#else // !USE_MULTICORE

bool multicoreRegisterConsumer(multicoreConsumerDrainFn_t *drainFn, void *ctx)
{
    UNUSED(drainFn);
    UNUSED(ctx);
    return false; // no core 1 to run it on
}

void multicoreSignalWork(void)
{
    // no core 1 to wake
}

bool multicoreCheckCore1Alive(void)
{
    return false;
}

bool multicoreIsCore1Alive(void)
{
    return false;
}

bool multicoreEnableIrqOnCore1(uint irqNum)
{
    UNUSED(irqNum);
    return false; // no core 1 to service it
}

uint32_t multicoreGetHeartbeat(void)
{
    return 0;
}
#endif // USE_MULTICORE


void multicoreExecuteBlocking(core1_func_t *func)
{
#ifdef USE_MULTICORE
    core_message_t msg;
    msg.command = MULTICORE_CMD_FUNC_BLOCKING;
    msg.func = func;

    bool result;

    queue_add_blocking(&core1_queue, &msg);
    multicoreSignalWork();
    // Wait for the command to complete
    queue_remove_blocking(&core0_queue, &result);
#else
    // If multicore is not used, execute the command directly
    if (func) {
        func();
    }
#endif // USE_MULTICORE
}

void multicoreExecute(core1_func_t *func)
{
#ifdef USE_MULTICORE
    core_message_t msg;
    msg.command = MULTICORE_CMD_FUNC;
    msg.func = func;

    queue_add_blocking(&core1_queue, &msg);
    multicoreSignalWork();
#else
    // If multicore is not used, execute the command directly
    if (func) {
        func();
    }
#endif // USE_MULTICORE
}

