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

// AT32F435/437 DSHOT driver -- native AT-BSP equivalent of drivers/pwm_output_dshot.c.
// pwm_output_dshot.c is deeply coupled to STM32 StdPeriph API/register names that have no
// 1:1 AT-BSP equivalent (TIM_TimeBaseInit/TIM_ICInit/TIM_DMAConfig/TIM_DMABase_CCR1/
// ->DMAR burst register/DMA_StructInit/etc) and cannot be shimmed incrementally, exactly
// like drivers/timer.c. This file is a full parallel logic port using AT-BSP calls
// instead, mutually exclusive with drivers/pwm_output_dshot.c via make/mcu/AT32F4.mk's
// MCU_EXCLUDES (see drivers/timer_at32bsp.c for the precedent).
//
// Scope of this initial port (see AT32F435_TODO.md for follow-up phases):
//   - Implements classic single-channel "one DMA request per timer channel, DMA writes
//     directly into that channel's CCR register" DSHOT output only.
//   - USE_DSHOT_DMAR (4-channel timer-update-event burst DMA into consecutive CCR
//     registers via the STM32 ->DMAR alias register) is NOT implemented here. AT-BSP
//     timers have no register equivalent to STM32's DMAR burst-address alias, so this
//     would need a different implementation strategy; target/common_pre.h intentionally
//     does not enable USE_DSHOT_DMAR for AT32F43x yet.
//   - USE_DSHOT_TELEMETRY (bidirectional DSHOT / ESC telemetry, which periodically
//     switches the timer channel between output-compare and input-capture) is NOT
//     implemented here; target/common_pre.h does not enable it for AT32F43x yet.

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "platform.h"

#ifdef USE_DSHOT

#include "build/debug.h"

#include "drivers/dma.h"
#include "drivers/dma_reqmap.h"
#include "drivers/io.h"
#include "drivers/nvic.h"
#include "rcc.h"
#include "drivers/time.h"
#include "drivers/timer.h"
#include "drivers/system.h"

#include "pwm_output.h"
#include "drivers/dshot.h"
#include "drivers/dshot_dpwm.h"
#include "drivers/dshot_command.h"

#include "pwm_output_dshot_shared.h"

FAST_CODE void pwmDshotSetDirectionOutput(
    motorDmaOutput_t * const motor,
    TIM_OCInitTypeDef *pOcInit, DMA_InitTypeDef* pDmaInit
)
{
    const timerHardware_t * const timerHardware = motor->timerHardware;
    TIM_TypeDef *timer = timerHardware->tim;

    dmaResource_t *dmaRef = motor->dmaRef;

    xDMA_DeInit(dmaRef);

    timerOCPreloadConfig(timer, timerHardware->channel, DISABLE);
    timerOCInit(timer, timerHardware->channel, pOcInit);
    timerOCPreloadConfig(timer, timerHardware->channel, ENABLE);

    xDMA_Init(dmaRef, pDmaInit);
    xDMA_ITConfig(dmaRef, DMA_IT_TCIF, ENABLE);
}

void pwmCompleteDshotMotorUpdate(void)
{
    /* If there is a dshot command loaded up, time it correctly with motor update*/
    if (!dshotCommandQueueEmpty()) {
        if (!dshotCommandOutputIsEnabled(dshotPwmDevice.count)) {
            return;
        }
    }

    for (int i = 0; i < dmaMotorTimerCount; i++) {
        tmr_period_buffer_enable(dmaMotorTimers[i].timer, DISABLE);
        tmr_period_value_set(dmaMotorTimers[i].timer, dmaMotorTimers[i].outputPeriod);
        tmr_period_buffer_enable(dmaMotorTimers[i].timer, ENABLE);
        tmr_counter_value_set(dmaMotorTimers[i].timer, 0);
        tmr_dma_request_enable(dmaMotorTimers[i].timer, (tmr_dma_request_type)dmaMotorTimers[i].timerDmaSources, ENABLE);
        dmaMotorTimers[i].timerDmaSources = 0;
    }
}

FAST_CODE static void motor_DMA_IRQHandler(dmaChannelDescriptor_t *descriptor)
{
    if (DMA_GET_FLAG_STATUS(descriptor, DMA_IT_TCIF)) {
        motorDmaOutput_t * const motor = &dmaMotors[descriptor->userParam];

        xDMA_Cmd(motor->dmaRef, DISABLE);
        tmr_dma_request_enable(motor->timerHardware->tim, (tmr_dma_request_type)motor->timerDmaSource, DISABLE);

        DMA_CLEAR_FLAG(descriptor, DMA_IT_TCIF);
    }
}

bool pwmDshotMotorHardwareConfig(const timerHardware_t *timerHardware, uint8_t motorIndex, motorPwmProtocolTypes_e pwmProtocolType, uint8_t output)
{
    TIM_OCInitTypeDef ocInitStruct;
    DMA_InitTypeDef   dmaInitStruct;

    dmaResource_t *dmaRef = NULL;

#if defined(USE_DMA_SPEC)
    const dmaChannelSpec_t *dmaSpec = dmaGetChannelSpecByTimer(timerHardware);

    if (dmaSpec != NULL) {
        dmaRef = dmaSpec->ref;
    }
#else
    dmaRef = timerHardware->dmaRef;
#endif

    if (dmaRef == NULL) {
        return false;
    }

    dmaIdentifier_e dmaIdentifier = dmaGetIdentifier(dmaRef);

    if (!dmaAllocate(dmaIdentifier, OWNER_MOTOR, RESOURCE_INDEX(motorIndex))) {
        return false;
    }

    motorDmaOutput_t * const motor = &dmaMotors[motorIndex];
    TIM_TypeDef *timer = timerHardware->tim;

    // Boolean configureTimer is always true when different channels of the same timer are processed in sequence,
    // causing the timer and the associated DMA initialized more than once.
    // To fix this, getTimerIndex must be expanded to return if a new timer has been requested.
    // However, since the initialization is idempotent, it is left as is in a favor of flash space (for now).
    const uint8_t timerIndex = getTimerIndex(timer);
    const bool configureTimer = (timerIndex == dmaMotorTimerCount-1);

    motor->timer = &dmaMotorTimers[timerIndex];
    motor->index = motorIndex;
    motor->timerHardware = timerHardware;

    const IO_t motorIO = IOGetByTag(timerHardware->tag);

    const uint8_t pupMode = (output & TIMER_OUTPUT_INVERTED) ? 1 : 0;

    motor->iocfg = pupMode ? IOCFG_AF_PP_PD : IOCFG_AF_PP_UP;
    IOConfigGPIOAF(motorIO, motor->iocfg, timerHardware->alternateFunction);

    // Outside of any DSHOT_TELEMETRY support, this must always be set (not just when a
    // direction switch back from input-capture is possible) since it is reloaded into the
    // timer's period register on every motor update in pwmCompleteDshotMotorUpdate().
    motor->timer->outputPeriod = (pwmProtocolType == PWM_TYPE_PROSHOT1000 ? (MOTOR_NIBBLE_LENGTH_PROSHOT) : MOTOR_BITLENGTH) - 1;

    if (configureTimer) {
        RCC_ClockCmd(timerRCC(timer), ENABLE);
        tmr_counter_enable(timer, FALSE);

        const uint32_t prescaler = (uint32_t)(lrintf((float) timerClock(timer) / getDshotHz(pwmProtocolType) + 0.01f) - 1);
        tmr_base_init(timer, motor->timer->outputPeriod, prescaler);
        tmr_clock_source_div_set(timer, TMR_CLOCK_DIV1);
        tmr_cnt_dir_set(timer, TMR_COUNT_UP);
    }

    tmr_output_default_para_init(&ocInitStruct);
    ocInitStruct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
    if (output & TIMER_OUTPUT_N_CHANNEL) {
        ocInitStruct.occ_output_state = TRUE;
        ocInitStruct.occ_idle_state = FALSE;
        ocInitStruct.occ_polarity = (output & TIMER_OUTPUT_INVERTED) ? TMR_OUTPUT_ACTIVE_LOW : TMR_OUTPUT_ACTIVE_HIGH;
    } else {
        ocInitStruct.oc_output_state = TRUE;
        ocInitStruct.oc_idle_state = TRUE;
        ocInitStruct.oc_polarity = (output & TIMER_OUTPUT_INVERTED) ? TMR_OUTPUT_ACTIVE_LOW : TMR_OUTPUT_ACTIVE_HIGH;
    }

    motor->timerDmaSource = timerDmaSource(timerHardware->channel);
    motor->timer->timerDmaSources &= ~motor->timerDmaSource;

    xDMA_Cmd(dmaRef, DISABLE);
    xDMA_DeInit(dmaRef);

    dmaEnable(dmaIdentifier);
#if defined(USE_DMA_SPEC)
    dmaMuxEnable(dmaIdentifier, dmaSpec->channel);
#endif

    motor->dmaBuffer = &dshotDmaBuffer[motorIndex][0];

    dma_default_para_init(&dmaInitStruct);
    dmaInitStruct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
    dmaInitStruct.loop_mode_enable = FALSE;
    dmaInitStruct.peripheral_base_addr = (uint32_t)timerChCCR(timerHardware);
    dmaInitStruct.peripheral_inc_enable = FALSE;
    dmaInitStruct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_WORD;
    dmaInitStruct.memory_base_addr = (uint32_t)motor->dmaBuffer;
    dmaInitStruct.memory_inc_enable = TRUE;
    dmaInitStruct.memory_data_width = DMA_MEMORY_DATA_WIDTH_WORD;
    dmaInitStruct.buffer_size = (pwmProtocolType == PWM_TYPE_PROSHOT1000) ? PROSHOT_DMA_BUFFER_SIZE : DSHOT_DMA_BUFFER_SIZE;
    dmaInitStruct.priority = DMA_PRIORITY_HIGH;

    motor->dmaRef = dmaRef;

    pwmDshotSetDirectionOutput(motor, &ocInitStruct, &dmaInitStruct);

    dmaSetHandler(dmaIdentifier, motor_DMA_IRQHandler, NVIC_PRIO_DSHOT_DMA, motor->index);

    tmr_counter_enable(timer, TRUE);
    // Unlike STM32 StdPeriph's TIM_OCInit() (which only programs CCMR bits, requiring a
    // separate TIM_CCxCmd()/TIM_CCxNCmd() call to set the CCER channel-enable bits),
    // AT-BSP's tmr_output_channel_config() above already applies oc_output_state/
    // occ_output_state (the "output channel enable"/"output channel complementary enable"
    // fields) as part of the same call, so no separate per-channel enable call is needed
    // here for either the main or complementary output.
    if (configureTimer) {
        tmr_period_buffer_enable(timer, ENABLE);
        tmr_output_enable(timer, TRUE);
        tmr_counter_enable(timer, TRUE);
    }

    motor->configured = true;

    return true;
}

#endif
