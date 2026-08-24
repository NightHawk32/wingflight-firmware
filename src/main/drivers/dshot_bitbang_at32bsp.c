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

// AT-BSP-native counterpart to drivers/dshot_bitbang_stdperiph.c (STM32 StdPeriph) /
// drivers/dshot_bitbang_ll.c (STM32 HAL/LL), providing the low-level MCU-specific bb*
// functions used by the shared, MCU-agnostic drivers/dshot_bitbang.c. Ported from
// betaflight's own real, working upstream AT32 implementation
// (betaflight/src/platform/AT32/dshot_bitbang_stdperiph.c), cross-checked against this
// repo's own STM32 StdPeriph reference file for exact conventions/signatures.

#include <stdint.h>
#include <math.h>
#include <string.h>

#include "platform.h"

#ifdef USE_DSHOT_BITBANG

#include "build/atomic.h"
#include "build/debug.h"
#include "build/debug_pin.h"

#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/dma.h"
#include "drivers/dma_reqmap.h"
#include "drivers/dshot.h"
#include "drivers/dshot_bitbang_impl.h"
#include "drivers/dshot_command.h"
#include "drivers/motor.h"
#include "drivers/nvic.h"
#include "drivers/pwm_output.h" // XXX for pwmOutputPort_t motors[]; should go away with refactoring
#include "drivers/time.h"
#include "drivers/timer.h"

#include "pg/motor.h"

// Convert Wingflight's byte-offset channel (0/4/8/12) to AT-BSP's tmr_channel_select_type
// (0/2/4/6) -- same conversion as timer_at32bsp.c's private AT_CH_SELECT(), redefined here
// locally to avoid adding a new cross-file header dependency for one line of arithmetic.
#define BB_CH_SELECT(ch) ((tmr_channel_select_type)((ch) >> 1))

void bbGpioSetup(bbMotor_t *bbMotor)
{
    bbPort_t *bbPort = bbMotor->bbPort;
    int pinIndex = bbMotor->pinIndex;

    bbPort->gpioModeMask |= (0x03 << (pinIndex * 2));
    bbPort->gpioModeInput |= (GPIO_MODE_INPUT << (pinIndex * 2));
    bbPort->gpioModeOutput |= (GPIO_MODE_OUTPUT << (pinIndex * 2));

#ifdef USE_DSHOT_TELEMETRY
    if (useDshotTelemetry) {
        bbPort->gpioIdleBSRR |= (1 << pinIndex);         // BS (lower half)
    } else
#endif
    {
        bbPort->gpioIdleBSRR |= (1 << (pinIndex + 16));  // BR (higher half)
    }

#ifdef USE_DSHOT_TELEMETRY
    if (useDshotTelemetry) {
        IOWrite(bbMotor->io, 1);
    } else
#endif
    {
        IOWrite(bbMotor->io, 0);
    }

    IOConfigGPIO(bbMotor->io, IO_CONFIG(GPIO_MODE_OUTPUT, GPIO_DRIVE_STRENGTH_STRONGER, GPIO_OUTPUT_PUSH_PULL, bbPuPdMode));
}

void bbTimerChannelInit(bbPort_t *bbPort)
{
    const timerHardware_t *timhw = bbPort->timhw;

    TIM_OCInitTypeDef TIM_OCStruct;

    tmr_output_default_para_init(&TIM_OCStruct);

    TIM_OCStruct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
    TIM_OCStruct.oc_idle_state = TRUE;
    TIM_OCStruct.oc_output_state = TRUE;
    TIM_OCStruct.oc_polarity = TMR_OUTPUT_ACTIVE_LOW;

    tmr_channel_value_set((tmr_type *)timhw->tim, BB_CH_SELECT(timhw->channel), 10);

    tmr_counter_enable((tmr_type *)timhw->tim, FALSE);

    timerOCInit((tmr_type *)timhw->tim, timhw->channel, &TIM_OCStruct);

    tmr_channel_enable((tmr_type *)timhw->tim, BB_CH_SELECT(timhw->channel), TRUE);

    timerOCPreloadConfig((tmr_type *)timhw->tim, timhw->channel, TRUE);

#ifdef DEBUG_MONITOR_PACER
    if (timhw->tag) {
        IO_t io = IOGetByTag(timhw->tag);
        IOConfigGPIOAF(io, IOCFG_AF_PP, timhw->alternateFunction);
        IOInit(io, OWNER_DSHOT_BITBANG, 0);
        // AT-BSP equivalent of StdPeriph's TIM_CtrlPWMOutputs() (see pwm_output.c's own
        // AT32 branch for the same substitution).
        tmr_output_enable((tmr_type *)timhw->tim, TRUE);
    }
#endif

    // Enable and keep it running

    tmr_counter_enable((tmr_type *)timhw->tim, TRUE);
}

#ifdef USE_DMA_REGISTER_CACHE

static void bbLoadDMARegs(dmaResource_t *dmaResource, dmaRegCache_t *dmaRegCache)
{
    ((DMA_ARCH_TYPE *)dmaResource)->ctrl = dmaRegCache->ctrl;
    ((DMA_ARCH_TYPE *)dmaResource)->dtcnt = dmaRegCache->dtcnt;
    ((DMA_ARCH_TYPE *)dmaResource)->paddr = dmaRegCache->paddr;
    ((DMA_ARCH_TYPE *)dmaResource)->maddr = dmaRegCache->maddr;
}

static void bbSaveDMARegs(dmaResource_t *dmaResource, dmaRegCache_t *dmaRegCache)
{
    dmaRegCache->ctrl = ((DMA_ARCH_TYPE *)dmaResource)->ctrl;
    dmaRegCache->dtcnt = ((DMA_ARCH_TYPE *)dmaResource)->dtcnt;
    dmaRegCache->paddr = ((DMA_ARCH_TYPE *)dmaResource)->paddr;
    dmaRegCache->maddr = ((DMA_ARCH_TYPE *)dmaResource)->maddr;
}
#endif

void bbSwitchToOutput(bbPort_t * bbPort)
{
    dbgPinHi(1);
    // Output idle level before switching to output
    // Use the scr register for this (AT-BSP's BSRR equivalent: lower 16 bits set, upper 16
    // bits clear -- see IOIdleBSRR construction in bbGpioSetup())

    WRITE_REG(bbPort->gpio->scr, bbPort->gpioIdleBSRR);

    // Set GPIO to output
    ATOMIC_BLOCK(NVIC_PRIO_TIMER) {
        MODIFY_REG(bbPort->gpio->cfgr, bbPort->gpioModeMask, bbPort->gpioModeOutput);
    }

    // Reinitialize port group DMA for output

    dmaResource_t *dmaResource = bbPort->dmaResource;
#ifdef USE_DMA_REGISTER_CACHE
    bbLoadDMARegs(dmaResource, &bbPort->dmaRegOutput);
#else
    xDMA_DeInit(dmaResource);
    xDMA_Init(dmaResource, &bbPort->outputDmaInit);
    // Needs this, as it is DeInit'ed above...
    xDMA_ITConfig(dmaResource, DMA_IT_TCIF, ENABLE);
#endif

    // Reinitialize pacer timer for output

    ((tmr_type *)bbPort->timhw->tim)->pr = bbPort->outputARR;

    bbPort->direction = DSHOT_BITBANG_DIRECTION_OUTPUT;

    dbgPinLo(1);
}

#ifdef USE_DSHOT_TELEMETRY
void bbSwitchToInput(bbPort_t *bbPort)
{
    dbgPinHi(1);

    // Set GPIO to input

    ATOMIC_BLOCK(NVIC_PRIO_TIMER) {
        MODIFY_REG(bbPort->gpio->cfgr, bbPort->gpioModeMask, bbPort->gpioModeInput);
    }

    // Reinitialize port group DMA for input
    dmaResource_t *dmaResource = bbPort->dmaResource;
#ifdef USE_DMA_REGISTER_CACHE
    bbLoadDMARegs(dmaResource, &bbPort->dmaRegInput);
#else
    xDMA_DeInit(dmaResource);
    xDMA_Init(dmaResource, &bbPort->inputDmaInit);
    // Needs this, as it is DeInit'ed above...
    xDMA_ITConfig(dmaResource, DMA_IT_TCIF, ENABLE);
#endif

    // Reinitialize pacer timer for input
    ((tmr_type *)bbPort->timhw->tim)->cval = 0;
    ((tmr_type *)bbPort->timhw->tim)->pr = bbPort->inputARR;

    bbDMA_Cmd(bbPort, ENABLE);

    bbPort->direction = DSHOT_BITBANG_DIRECTION_INPUT;

    dbgPinLo(1);
}
#endif

void bbDMAPreconfigure(bbPort_t *bbPort, uint8_t direction)
{
    DMA_InitTypeDef *dmainit = (direction == DSHOT_BITBANG_DIRECTION_OUTPUT) ?  &bbPort->outputDmaInit : &bbPort->inputDmaInit;

    dma_default_para_init(dmainit);

    dmainit->loop_mode_enable = FALSE;
    dmainit->peripheral_inc_enable = FALSE;
    dmainit->memory_inc_enable = TRUE;

    if (direction == DSHOT_BITBANG_DIRECTION_OUTPUT) {
        dmainit->priority = DMA_PRIORITY_VERY_HIGH;
        dmainit->direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
        dmainit->buffer_size = bbPort->portOutputCount;
        dmainit->peripheral_base_addr = (uint32_t)&bbPort->gpio->scr;
        dmainit->peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_WORD;
        dmainit->memory_base_addr = (uint32_t)bbPort->portOutputBuffer;
        dmainit->memory_data_width = DMA_MEMORY_DATA_WIDTH_WORD;

#ifdef USE_DMA_REGISTER_CACHE
        xDMA_Init(bbPort->dmaResource, dmainit);
        bbSaveDMARegs(bbPort->dmaResource, &bbPort->dmaRegOutput);
#endif
    } else {
        dmainit->priority = DMA_PRIORITY_VERY_HIGH;
        dmainit->direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
        dmainit->buffer_size = bbPort->portInputCount;

        dmainit->peripheral_base_addr = (uint32_t)&bbPort->gpio->idt;
        dmainit->peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;
        dmainit->memory_base_addr = (uint32_t)bbPort->portInputBuffer;
        dmainit->memory_data_width = DMA_MEMORY_DATA_WIDTH_HALFWORD;

#ifdef USE_DMA_REGISTER_CACHE
        xDMA_Init(bbPort->dmaResource, dmainit);
        bbSaveDMARegs(bbPort->dmaResource, &bbPort->dmaRegInput);
#endif
    }
}

void bbTIM_TimeBaseInit(bbPort_t *bbPort, uint16_t period)
{
    // AT-BSP's tmr_base_init() takes scalar args directly, so bbPort->timeBaseInit (an
    // unused placeholder field for this MCU, see dshot_bitbang_impl.h) is never touched here.
    tmr_base_init((tmr_type *)bbPort->timhw->tim, period, 0);
    tmr_clock_source_div_set((tmr_type *)bbPort->timhw->tim, TMR_CLOCK_DIV1);
    tmr_cnt_dir_set((tmr_type *)bbPort->timhw->tim, TMR_COUNT_UP);
    tmr_period_buffer_enable((tmr_type *)bbPort->timhw->tim, TRUE);
}

void bbTIM_DMACmd(TIM_TypeDef *TIMx, uint16_t TIM_DMASource, FunctionalState NewState)
{
    tmr_dma_request_enable((tmr_type *)TIMx, (tmr_dma_request_type)TIM_DMASource, NewState ? TRUE : FALSE);
}

void bbDMA_ITConfig(bbPort_t *bbPort)
{
    xDMA_ITConfig(bbPort->dmaResource, DMA_IT_TCIF, ENABLE);
}

void bbDMA_Cmd(bbPort_t *bbPort, FunctionalState NewState)
{
    xDMA_Cmd(bbPort->dmaResource, NewState);
}

int bbDMA_Count(bbPort_t *bbPort)
{
    return xDMA_GetCurrDataCounter(bbPort->dmaResource);
}

#endif // USE_DSHOT_BITBANG
