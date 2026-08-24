/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * AT32F43x driver-layer implementation of the drivers/system.h interface,
 * adapted from betaflight/src/platform/AT32/system_at32f43x.c to Wingflight's
 * BF4.3-era driver contracts.
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#include "drivers/system.h"
#include "drivers/persistent.h"
#include "drivers/nvic.h"
#include "at32f435_437_clock.h"

void systemResetHard(void)
{
    __disable_irq();
    NVIC_SystemReset();
}

// The build links with -nostartfiles, so the newlib-nano crti.o/crtn.o objects that would
// normally provide _init()/_fini() are not linked in, yet startup_at32f435_437.s's Reset_Handler
// still calls __libc_init_array() (matching AT-BSP's own vendor startup template), which
// unconditionally calls _init(). Provide a trivial stub, matching betaflight's own AT32 platform
// glue (src/platform/AT32/startup/system_at32f435_437.c).
void _init(void)
{
}

typedef void resetHandler_t(void);

typedef struct isrVector_s {
    __I uint32_t    stackEnd;
    resetHandler_t *resetHandler;
} isrVector_t;

// Used by the startup file for AT32
void checkForBootLoaderRequest(void)
{
    uint32_t bootloaderRequest = persistentObjectRead(PERSISTENT_OBJECT_RESET_REASON);

    if (bootloaderRequest != RESET_BOOTLOADER_REQUEST_ROM) {
        return;
    }
    persistentObjectWrite(PERSISTENT_OBJECT_RESET_REASON, RESET_NONE);

    extern isrVector_t system_isr_vector_table_base;

    __set_MSP(system_isr_vector_table_base.stackEnd);
    system_isr_vector_table_base.resetHandler();
    while (1);
}

void enableGPIOPowerUsageAndNoiseReductions(void)
{
    crm_periph_clock_enable(
        CRM_GPIOA_PERIPH_CLOCK |
        CRM_GPIOB_PERIPH_CLOCK |
        CRM_GPIOC_PERIPH_CLOCK |
        CRM_GPIOD_PERIPH_CLOCK |
        CRM_GPIOE_PERIPH_CLOCK |
        CRM_GPIOF_PERIPH_CLOCK |
        CRM_GPIOG_PERIPH_CLOCK |
        CRM_GPIOH_PERIPH_CLOCK |
        CRM_DMA1_PERIPH_CLOCK  |
        CRM_DMA2_PERIPH_CLOCK  |
        0, TRUE);
}

bool isMPUSoftReset(void)
{
    return crm_flag_get(CRM_SW_RESET_FLAG) != RESET;
}

void systemInit(void)
{
    persistentObjectInit();

    checkForBootLoaderRequest();

    system_clock_config(); // 288MHz core, 48MHz USB - see at32f435_437_clock.c

    // Configure NVIC preempt/priority groups
    nvic_priority_group_config(NVIC_PRIORITY_GROUPING);

    // Although VTOR is already loaded with a possible vector table in RAM,
    // removing the call to nvic_vector_table_set causes USB not to become active.
    extern uint8_t isr_vector_table_base;
    nvic_vector_table_set((uint32_t)&isr_vector_table_base, 0x0);

    crm_periph_clock_enable(CRM_OTGFS2_PERIPH_CLOCK | CRM_OTGFS1_PERIPH_CLOCK, FALSE);

    CRM->ctrlsts_bit.rstfc = TRUE; // Clear reset flags

    enableGPIOPowerUsageAndNoiseReductions();

    // Init cycle counter
    cycleCounterInit();

    // SysTick
    SysTick_Config(system_core_clock / 1000);
}
