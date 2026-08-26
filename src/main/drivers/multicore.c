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
#include "platform/multicore.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/flash.h"

// dma_pico.c: unmasks DMA_IRQ_1 on core 1's own NVIC once core 1 is running
// (see the core-affinity note in dma_pico.c's dmaSetHandler()).
void dmaCore1IrqInit(void);

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
static uint8_t multicoreConsumerCount;

static volatile uint32_t multicoreHeartbeat;

bool multicoreRegisterConsumer(multicoreConsumerDrainFn_t *drainFn, void *ctx)
{
    if (!drainFn || multicoreConsumerCount >= MAX_MULTICORE_CONSUMERS) {
        return false;
    }
    multicoreConsumers[multicoreConsumerCount].drainFn = drainFn;
    multicoreConsumers[multicoreConsumerCount].ctx = ctx;
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

    // Each Cortex-M33 core has its own independent NVIC: DMA_IRQ_CORE_NUM 1
    // (RP2350_UNIFIED/target.h) means DMA completions should be serviced
    // here, but core 0's dmaSetHandler() calls can only unmask the IRQ on
    // core 0's own NVIC - core 1 must do it for itself. See dma_pico.c.
    dmaCore1IrqInit();

    // This loop is run on the second core. For now the RPC consumer below IS
    // core 1's task loop; dedicated lock-free ring-buffer consumers for
    // latency-tolerant work (USB-MSC block I/O, blackbox flush, CLI/MSP
    // parsing) are designed in docs/RP2350-Porting-Plan.md's "Core-1 task
    // consumer design" and get added here as those producers materialise.
    while (true) {

        core_message_t msg;
        if (queue_try_remove(&core1_queue, &msg)) {
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
            case MULTICORE_CMD_STOP:
                flash_safe_execute_core_deinit();
                multicore_reset_core1();
                return; // Exit the core1_main function
            default:
                // unknown command or none
                break;
            }
        }

        // Round-robin pump: one bounded drain call per registered consumer
        // per pass, so a busy consumer can't starve the others or the RPC
        // queue above. Each drainFn is responsible for its own bound (e.g.
        // "at most N bytes this call") - this loop does not enforce one.
        for (uint8_t i = 0; i < multicoreConsumerCount; i++) {
            multicoreConsumers[i].drainFn(multicoreConsumers[i].ctx);
        }

        multicoreHeartbeat++;

        tight_loop_contents();
    }
}

void multicoreStart(void)
{
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
 }
#else // !USE_MULTICORE

bool multicoreRegisterConsumer(multicoreConsumerDrainFn_t *drainFn, void *ctx)
{
    UNUSED(drainFn);
    UNUSED(ctx);
    return false; // no core 1 to run it on
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
#else
    // If multicore is not used, execute the command directly
    if (func) {
        func();
    }
#endif // USE_MULTICORE
}

