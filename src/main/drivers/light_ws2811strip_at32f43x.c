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
 * If not, see <http://www.gnu.org/licenses/>.
 */

// AT32F435/437 WS2811 LED strip driver -- native AT-BSP equivalent of
// drivers/light_ws2811strip_stdperiph.c. Deeply coupled to StdPeriph-only calls/register names
// (TIM_TimeBaseInit, TIM_CCxNCmd/TIM_CCxCmd, TIM_DMACmd, TIM_SetCounter, DMA_StructInit) with no
// AT-BSP equivalent, same class of problem as pwm_output_dshot.c -- full replacement file,
// mutually exclusive via simply not listing light_ws2811strip_stdperiph.c in AT32F4.mk.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_LED_STRIP

#include "build/debug.h"

#include "common/color.h"

#include "drivers/dma.h"
#include "drivers/dma_reqmap.h"
#include "drivers/io.h"
#include "drivers/nvic.h"
#include "drivers/rcc.h"
#include "drivers/timer.h"

#include "light_ws2811strip.h"

static IO_t ws2811IO = IO_NONE;
static dmaResource_t *dmaRef = NULL;
static TIM_TypeDef *timer = NULL;

static void WS2811_DMA_IRQHandler(dmaChannelDescriptor_t *descriptor)
{
#if defined(USE_WS2811_SINGLE_COLOUR)
    static uint32_t counter = 0;
#endif

    if (DMA_GET_FLAG_STATUS(descriptor, DMA_IT_TCIF)) {
#if defined(USE_WS2811_SINGLE_COLOUR)
        counter++;
        if (counter == WS2811_LED_STRIP_LENGTH) {
            // Output low for 50us delay
            memset(ledStripDMABuffer, 0, sizeof(ledStripDMABuffer));
        } else if (counter == (WS2811_LED_STRIP_LENGTH + WS2811_DELAY_ITERATIONS)) {
            counter = 0;
            ws2811LedDataTransferInProgress = false;
            xDMA_Cmd(descriptor->ref, DISABLE);
        }
#else
        ws2811LedDataTransferInProgress = false;
        xDMA_Cmd(descriptor->ref, DISABLE);
#endif

        DMA_CLEAR_FLAG(descriptor, DMA_IT_TCIF);
    }
}

bool ws2811LedStripHardwareInit(ioTag_t ioTag)
{
    if (!ioTag) {
        return false;
    }

    TIM_OCInitTypeDef TIM_OCInitStructure;
    DMA_InitTypeDef DMA_InitStructure;

    const timerHardware_t *timerHardware = timerAllocate(ioTag, OWNER_LED_STRIP, 0);

    if (timerHardware == NULL) {
        return false;
    }

    timer = timerHardware->tim;

#if defined(USE_DMA_SPEC)
    const dmaChannelSpec_t *dmaSpec = dmaGetChannelSpecByTimer(timerHardware);

    if (dmaSpec == NULL) {
        return false;
    }

    dmaRef = dmaSpec->ref;
#else
    dmaRef = timerHardware->dmaRef;
#endif

    if (dmaRef == NULL || !dmaAllocate(dmaGetIdentifier(dmaRef), OWNER_LED_STRIP, 0)) {
        return false;
    }

    dmaIdentifier_e dmaIdentifier = dmaGetIdentifier(dmaRef);

    ws2811IO = IOGetByTag(ioTag);
    IOInit(ws2811IO, OWNER_LED_STRIP, 0);
    IOConfigGPIOAF(ws2811IO, IOCFG_AF_PP_UP, timerHardware->alternateFunction);

    RCC_ClockCmd(timerRCC(timer), ENABLE);

    tmr_counter_enable(timer, FALSE);

    /* Compute the prescaler value */
    uint16_t prescaler = timerGetPrescalerByDesiredMhz(timer, WS2811_TIMER_MHZ);
    uint16_t period = timerGetPeriodByPrescaler(timer, prescaler, WS2811_CARRIER_HZ);

    BIT_COMPARE_1 = period / 3 * 2;
    BIT_COMPARE_0 = period / 3;

    /* Time base configuration */
    tmr_base_init(timer, period, prescaler);
    tmr_clock_source_div_set(timer, TMR_CLOCK_DIV1);
    tmr_cnt_dir_set(timer, TMR_COUNT_UP);

    /* PWM1 Mode configuration */
    tmr_output_default_para_init(&TIM_OCInitStructure);
    TIM_OCInitStructure.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;

    if (timerHardware->output & TIMER_OUTPUT_N_CHANNEL) {
        TIM_OCInitStructure.occ_output_state = TRUE;
        TIM_OCInitStructure.occ_idle_state = FALSE;
        TIM_OCInitStructure.occ_polarity = (timerHardware->output & TIMER_OUTPUT_INVERTED) ? TMR_OUTPUT_ACTIVE_LOW : TMR_OUTPUT_ACTIVE_HIGH;
    } else {
        TIM_OCInitStructure.oc_output_state = TRUE;
        TIM_OCInitStructure.oc_idle_state = TRUE;
        TIM_OCInitStructure.oc_polarity = (timerHardware->output & TIMER_OUTPUT_INVERTED) ? TMR_OUTPUT_ACTIVE_LOW : TMR_OUTPUT_ACTIVE_HIGH;
    }

    timerOCInit(timer, timerHardware->channel, &TIM_OCInitStructure);
    timerOCPreloadConfig(timer, timerHardware->channel, ENABLE);
    tmr_channel_value_set(timer, (tmr_channel_select_type)(timerHardware->channel >> 1), 0);

    tmr_output_enable(timer, TRUE);
    tmr_period_buffer_enable(timer, ENABLE);
    tmr_counter_enable(timer, TRUE);

    dmaEnable(dmaIdentifier);
#if defined(USE_DMA_SPEC)
    dmaMuxEnable(dmaIdentifier, dmaSpec->channel);
#endif
    dmaSetHandler(dmaIdentifier, WS2811_DMA_IRQHandler, NVIC_PRIO_WS2811_DMA, 0);

    /* configure DMA */
    xDMA_Cmd(dmaRef, DISABLE);
    xDMA_DeInit(dmaRef);
    dma_default_para_init(&DMA_InitStructure);
    DMA_InitStructure.peripheral_base_addr = (uint32_t)timerCCR(timer, timerHardware->channel);
    DMA_InitStructure.buffer_size = WS2811_DMA_BUFFER_SIZE;
    DMA_InitStructure.peripheral_inc_enable = FALSE;
    DMA_InitStructure.memory_inc_enable = TRUE;
    DMA_InitStructure.memory_base_addr = (uint32_t)ledStripDMABuffer;
    DMA_InitStructure.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
    DMA_InitStructure.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_WORD;
    DMA_InitStructure.memory_data_width = DMA_MEMORY_DATA_WIDTH_WORD;
    DMA_InitStructure.priority = DMA_PRIORITY_VERY_HIGH;
#if defined(USE_WS2811_SINGLE_COLOUR)
    DMA_InitStructure.loop_mode_enable = TRUE;
#else
    DMA_InitStructure.loop_mode_enable = FALSE;
#endif

    xDMA_Init(dmaRef, &DMA_InitStructure);
    tmr_dma_request_enable(timer, (tmr_dma_request_type)timerDmaSource(timerHardware->channel), ENABLE);
    xDMA_ITConfig(dmaRef, DMA_IT_TCIF, ENABLE);

    return true;
}

void ws2811LedStripDMAEnable(void)
{
    xDMA_SetCurrDataCounter(dmaRef, WS2811_DMA_BUFFER_SIZE);  // load number of bytes to be transferred
    tmr_counter_value_set(timer, 0);
    tmr_counter_enable(timer, TRUE);
    xDMA_Cmd(dmaRef, ENABLE);
}
#endif
