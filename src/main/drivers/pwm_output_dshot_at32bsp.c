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
// Scope of this port (see AT32F435_TODO.md for follow-up phases):
//   - Implements classic single-channel "one DMA request per timer channel, DMA writes
//     directly into that channel's CCR register" DSHOT output.
//   - Implements USE_DSHOT_TELEMETRY / USE_DSHOT_TELEMETRY_STATS (bidirectional DSHOT /
//     ESC telemetry, periodically switching the timer channel between output-compare and
//     input-capture), ported from betaflight's own real, working upstream AT32 telemetry
//     implementation (betaflight/src/platform/AT32/pwm_output_dshot.c).
//   - USE_DSHOT_DMAR (4-channel timer-update-event burst DMA into consecutive CCR
//     registers via the STM32 ->DMAR alias register) is NOT implemented here. AT-BSP
//     timers have no register equivalent to STM32's DMAR burst-address alias, and
//     betaflight's own upstream AT32 DMAR implementation is itself admittedly
//     broken/untested ("// NB burst mode not tested", with the TIM_DMA_Update equivalent
//     call commented out) -- porting known-broken code for safety-critical motor-output
//     timing would be irresponsible. target/common_pre.h intentionally does not enable
//     USE_DSHOT_DMAR for AT32F43x.

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

// Maps a 1-based Betaflight timer channel number (+ "use the complementary/N output"
// flag) to the AT-BSP tmr_channel_select_type enum value needed by tmr_channel_enable()/
// tmr_channel_value_set()/tmr_input_channel_init(). Ported from betaflight's own AT32
// dshot driver (toCHSelectType()) -- timer_at32bsp.c's file-local AT_CH_SELECT() macro
// can't be reused here since it only handles normal (non-complementary) channels.
static tmr_channel_select_type toCHSelectType(uint8_t channel, bool useNChannel)
{
    if (useNChannel) {
        switch (channel) {
        case 1:
            return TMR_SELECT_CHANNEL_1C;
        case 2:
            return TMR_SELECT_CHANNEL_2C;
        case 3:
        default:
            return TMR_SELECT_CHANNEL_3C; // no complementary output on channel 4
        }
    }

    switch (channel) {
    case 1:
        return TMR_SELECT_CHANNEL_1;
    case 2:
        return TMR_SELECT_CHANNEL_2;
    case 3:
        return TMR_SELECT_CHANNEL_3;
    case 4:
    default:
        return TMR_SELECT_CHANNEL_4;
    }
}

#ifdef USE_DSHOT_TELEMETRY
// Called once per dshotPwmDevInit() (see pwm_output_dshot_shared.c's
// pwmStartDshotMotorUpdate()) to put every motor's timer channel into "compare" mode and
// enable it, mirroring betaflight's own AT32 dshotEnableChannels().
void dshotEnableChannels(uint8_t motorCount)
{
    for (int i = 0; i < motorCount; i++) {
        const timerHardware_t * const timerHardware = dmaMotors[i].timerHardware;

        tmr_primary_mode_select(timerHardware->tim, TMR_PRIMARY_SEL_COMPARE);
        tmr_channel_enable(timerHardware->tim, toCHSelectType(timerHardware->channel, (timerHardware->output & TIMER_OUTPUT_N_CHANNEL) != 0), TRUE);
    }
}
#endif

FAST_CODE void pwmDshotSetDirectionOutput(
    motorDmaOutput_t * const motor
#ifndef USE_DSHOT_TELEMETRY
    , TIM_OCInitTypeDef *pOcInit, DMA_InitTypeDef* pDmaInit
#endif
)
{
#ifdef USE_DSHOT_TELEMETRY
    TIM_OCInitTypeDef *pOcInit = &motor->ocInitStruct;
    DMA_InitTypeDef *pDmaInit = &motor->dmaInitStruct;
#endif

    const timerHardware_t * const timerHardware = motor->timerHardware;
    TIM_TypeDef *timer = timerHardware->tim;
    const uint8_t channel = timerHardware->channel;
    const bool useNChannel = (timerHardware->output & TIMER_OUTPUT_N_CHANNEL) != 0;
    const tmr_channel_select_type chSelect = toCHSelectType(channel, useNChannel);

    dmaResource_t *dmaRef = motor->dmaRef;

    xDMA_DeInit(dmaRef);

#ifdef USE_DSHOT_TELEMETRY
    motor->isInput = false;
#endif

    timerOCPreloadConfig(timer, channel, DISABLE);
    tmr_channel_enable(timer, chSelect, FALSE);

    // AT-BSP's tmr_output_channel_config() (called from timerOCInit() below) programs the
    // channel's output-compare mode bits, but -- unlike STM32 StdPeriph's TIM_OCInit() --
    // does not clear the channel's CxC "capture/compare selection" field in the cm1/cm2
    // register if pwmDshotSetDirectionInput() previously switched it to input-capture
    // mode (probably an AT-BSP SDK bug; betaflight's own AT32 port works around it
    // identically). Force it back to output (00) directly before reconfiguring.
    switch (channel) {
    case 1:
        timer->cm1_output_bit.c1c = 0;
        break;
    case 2:
        timer->cm1_output_bit.c2c = 0;
        break;
    case 3:
        timer->cm2_output_bit.c3c = 0;
        break;
    case 4:
    default:
        timer->cm2_output_bit.c4c = 0;
        break;
    }

    timerOCInit(timer, channel, pOcInit);
    tmr_channel_value_set(timer, toCHSelectType(channel, false), 0);
    tmr_channel_enable(timer, chSelect, TRUE);
    timerOCPreloadConfig(timer, channel, ENABLE);

    xDMA_Init(dmaRef, pDmaInit);
    xDMA_ITConfig(dmaRef, DMA_IT_TCIF, ENABLE);
}

#ifdef USE_DSHOT_TELEMETRY
// Switches a motor's timer channel from output-compare (DSHOT command output) to
// input-capture (bidirectional DSHOT / ESC telemetry GCR edge capture), ported from
// betaflight's own AT32 pwmDshotSetDirectionInput().
FAST_CODE static void pwmDshotSetDirectionInput(motorDmaOutput_t * const motor)
{
    DMA_InitTypeDef *pDmaInit = &motor->dmaInitStruct;
    const timerHardware_t * const timerHardware = motor->timerHardware;
    TIM_TypeDef *timer = timerHardware->tim;

    dmaResource_t *dmaRef = motor->dmaRef;

    xDMA_DeInit(dmaRef);

    motor->isInput = true;
    if (!inputStampUs) {
        inputStampUs = micros();
    }

    tmr_period_buffer_enable(timer, FALSE);
    timer->pr = 0xffffffff;

    tmr_input_channel_init(timer, &motor->icInitStruct, TMR_CHANNEL_INPUT_DIV_1);

    pDmaInit->direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
    xDMA_Init(dmaRef, pDmaInit);
}
#endif

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

#ifdef USE_DSHOT_TELEMETRY
        dshotDMAHandlerCycleCounters.irqAt = getCycleCounter();
#endif

        xDMA_Cmd(motor->dmaRef, DISABLE);
        tmr_dma_request_enable(motor->timerHardware->tim, (tmr_dma_request_type)motor->timerDmaSource, DISABLE);

        DMA_CLEAR_FLAG(descriptor, DMA_IT_TCIF);

#ifdef USE_DSHOT_TELEMETRY
        if (useDshotTelemetry) {
            pwmDshotSetDirectionInput(motor);
            xDMA_SetCurrDataCounter(motor->dmaRef, GCR_TELEMETRY_INPUT_LEN);
            xDMA_Cmd(motor->dmaRef, ENABLE);
            tmr_dma_request_enable(motor->timerHardware->tim, (tmr_dma_request_type)motor->timerDmaSource, ENABLE);
            dshotDMAHandlerCycleCounters.changeDirectionCompletedAt = getCycleCounter();
        }
#endif
    }
}


bool pwmDshotMotorHardwareConfig(const timerHardware_t *timerHardware, uint8_t motorIndex, motorPwmProtocolTypes_e pwmProtocolType, uint8_t output)
{
#ifdef USE_DSHOT_TELEMETRY
#define OCINIT motor->ocInitStruct
#define DMAINIT motor->dmaInitStruct
#else
    TIM_OCInitTypeDef ocInitStruct;
    DMA_InitTypeDef   dmaInitStruct;
#define OCINIT ocInitStruct
#define DMAINIT dmaInitStruct
#endif

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

#ifdef USE_DSHOT_TELEMETRY
    // Bidirectional DSHOT idles the line in the opposite polarity so the ESC can drive it
    // during telemetry input-capture; flip the polarity used for output-compare/GPIO pull
    // config accordingly (matches betaflight's own AT32 port), without touching the
    // timerHardware-level TIMER_OUTPUT_N_CHANNEL bit used elsewhere in this function.
    if (useDshotTelemetry) {
        output ^= TIMER_OUTPUT_INVERTED;
    }
#endif

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

    tmr_output_default_para_init(&OCINIT);
    OCINIT.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
    if (output & TIMER_OUTPUT_N_CHANNEL) {
        OCINIT.occ_output_state = TRUE;
        OCINIT.occ_idle_state = FALSE;
        OCINIT.occ_polarity = (output & TIMER_OUTPUT_INVERTED) ? TMR_OUTPUT_ACTIVE_LOW : TMR_OUTPUT_ACTIVE_HIGH;
    } else {
        OCINIT.oc_output_state = TRUE;
        OCINIT.oc_idle_state = TRUE;
        OCINIT.oc_polarity = (output & TIMER_OUTPUT_INVERTED) ? TMR_OUTPUT_ACTIVE_LOW : TMR_OUTPUT_ACTIVE_HIGH;
    }

#ifdef USE_DSHOT_TELEMETRY
    tmr_input_default_para_init(&motor->icInitStruct);
    motor->icInitStruct.input_mapped_select = TMR_CC_CHANNEL_MAPPED_DIRECT;
    motor->icInitStruct.input_polarity_select = TMR_INPUT_BOTH_EDGE;
    motor->icInitStruct.input_channel_select = toCHSelectType(timerHardware->channel, (output & TIMER_OUTPUT_N_CHANNEL) != 0);
    motor->icInitStruct.input_filter_value = 2;
#endif

    motor->timerDmaSource = timerDmaSource(timerHardware->channel);
    motor->timer->timerDmaSources &= ~motor->timerDmaSource;

    xDMA_Cmd(dmaRef, DISABLE);
    xDMA_DeInit(dmaRef);

    dmaEnable(dmaIdentifier);
#if defined(USE_DMA_SPEC)
    dmaMuxEnable(dmaIdentifier, dmaSpec->channel);
#endif

    motor->dmaBuffer = &dshotDmaBuffer[motorIndex][0];

    dma_default_para_init(&DMAINIT);
    DMAINIT.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
    DMAINIT.loop_mode_enable = FALSE;
    DMAINIT.peripheral_base_addr = (uint32_t)timerChCCR(timerHardware);
    DMAINIT.peripheral_inc_enable = FALSE;
    DMAINIT.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_WORD;
    DMAINIT.memory_base_addr = (uint32_t)motor->dmaBuffer;
    DMAINIT.memory_inc_enable = TRUE;
    DMAINIT.memory_data_width = DMA_MEMORY_DATA_WIDTH_WORD;
    DMAINIT.buffer_size = (pwmProtocolType == PWM_TYPE_PROSHOT1000) ? PROSHOT_DMA_BUFFER_SIZE : DSHOT_DMA_BUFFER_SIZE;
    DMAINIT.priority = DMA_PRIORITY_HIGH;

    motor->dmaRef = dmaRef;

#ifdef USE_DSHOT_TELEMETRY
    motor->dshotTelemetryDeadtimeUs = DSHOT_TELEMETRY_DEADTIME_US + 1000000 * (16 * MOTOR_BITLENGTH) / getDshotHz(pwmProtocolType);
    pwmDshotSetDirectionOutput(motor);
#else
    pwmDshotSetDirectionOutput(motor, &OCINIT, &DMAINIT);
#endif

    dmaSetHandler(dmaIdentifier, motor_DMA_IRQHandler, NVIC_PRIO_DSHOT_DMA, motor->index);

    tmr_counter_enable(timer, TRUE);
    // pwmDshotSetDirectionOutput() above already explicitly enables the timer channel
    // (tmr_channel_enable(..., TRUE)) as part of its output-direction setup -- needed
    // unconditionally now (not just under USE_DSHOT_TELEMETRY) since that function is also
    // responsible for switching back from input-capture mode after a telemetry read. No
    // separate per-channel enable call is needed here for either the main or complementary
    // output.
    if (configureTimer) {
        tmr_period_buffer_enable(timer, ENABLE);
        tmr_output_enable(timer, TRUE);
        tmr_counter_enable(timer, TRUE);
    }

#ifdef USE_DSHOT_TELEMETRY
    if (useDshotTelemetry) {
        // Avoid a high line during startup, which some ESCs interpret as bootloader entry.
        *timerChCCR(timerHardware) = 0xffff;
    }
#endif


    motor->configured = true;

    return true;
}

#endif
