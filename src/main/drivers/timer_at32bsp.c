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

// AT32F435/437 timer LOGIC driver -- this is the AT-BSP equivalent of drivers/timer.c
// (which is deeply coupled to STM32 StdPeriph API/register names and cannot be shimmed
// incrementally). It is mutually exclusive with drivers/timer.c via make/mcu/AT32F4.mk's
// MCU_EXCLUDES, exactly mirroring the drivers/timer_hal.c precedent used by STM32F7/H7/G4.
//
// Hardware definition tables (timerDefinitions[]/fullTimerHardware[]) live in the
// already-existing drivers/timer_at32f43x.c (the AT32 equivalent of timer_stm32f4xx.c),
// which also now provides timerClock().
//
// Channel encoding: Wingflight (like STM32) encodes timHw->channel as a BYTE offset into
// the timer's compare-register block: 0/4/8/12 for channels 1/2/3/4 (see
// CC_INDEX_FROM_CHANNEL()/CC_CHANNEL_FROM_INDEX() in timer.h and DEF_TIM_CHANNEL__D() in
// timer_def.h). AT-BSP's own tmr_channel_select_type enum instead uses 0/2/4/6 for the same
// four channels (TMR_SELECT_CHANNEL_1..4). The two encodings convert cleanly via a right
// shift by 1: channel(0/4/8/12) >> 1 == TMR_SELECT_CHANNEL_x(0/2/4/6). See AT_CH_SELECT()
// below -- this conversion was anticipated by a comment already present in timer_def.h's
// DEF_TIM_CHANNEL__D() macro.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "platform.h"

#ifdef USE_TIMER

#include "build/atomic.h"

#include "common/utils.h"

#include "drivers/nvic.h"

#include "drivers/io.h"
#include "rcc.h"
#include "drivers/system.h"

#include "timer.h"
#include "timer_impl.h"

#define TIM_N(n) (1 << (n))

/*
    Groups that allow running different period (ex 50Hz servos + 400Hz throttle + etc):
    TIM1 2 channels
    TIM2 4 channels
    TIM3 4 channels
    TIM4 4 channels
*/

#define USED_TIMER_COUNT BITCOUNT(USED_TIMERS)
#define CC_CHANNELS_PER_TIMER 4              // TIM_Channel_1..4

// Local byte-offset channel constants (same values STM32 headers provide as
// TIM_Channel_1..4), since AT-BSP has no equivalent public names.
#define TIM_Channel_1 0x00
#define TIM_Channel_2 0x04
#define TIM_Channel_3 0x08
#define TIM_Channel_4 0x0C

// Convert Wingflight's byte-offset channel (0/4/8/12) to AT-BSP's tmr_channel_select_type
// (0/2/4/6 for channels 1-4; see file header comment above).
#define AT_CH_SELECT(ch) ((tmr_channel_select_type)((ch) >> 1))

// TMR_Cx_INT and TMR_Cx_FLAG share identical bit values per channel (verified against
// lib/main/AT32F43x/drivers/inc/at32f435_437_tmr.h), so this single macro is valid both
// for enabling/disabling the interrupt and for reading/clearing the flag, exactly as
// TIM_IT_CCx served a dual purpose in the original STM32 code.
#define TIM_IT_CCx(ch) (TMR_C1_INT << ((ch) / 4))

typedef struct timerConfig_s {
    // per-timer
    timerOvrHandlerRec_t *updateCallback;

    // per-channel
    timerCCHandlerRec_t *edgeCallback[CC_CHANNELS_PER_TIMER];
    timerOvrHandlerRec_t *overflowCallback[CC_CHANNELS_PER_TIMER];

    // state
    timerOvrHandlerRec_t *overflowCallbackActive; // null-terminated linked list of active overflow callbacks
    uint32_t forcedOverflowTimerValue;
} timerConfig_t;
timerConfig_t timerConfig[USED_TIMER_COUNT];

typedef struct {
    channelType_t type;
} timerChannelInfo_t;

timerChannelInfo_t timerChannelInfo[TIMER_CHANNEL_COUNT];

typedef struct {
    uint8_t priority;
} timerInfo_t;
timerInfo_t timerInfo[USED_TIMER_COUNT];

// return index of timer in timer table. Lowest timer has index 0
#define TIMER_INDEX(i) BITCOUNT((TIM_N(i) - 1) & USED_TIMERS)

static uint8_t lookupTimerIndex(const TIM_TypeDef *tim)
{
#define _CASE_SHF 10           // amount we can safely shift timer address to the right. gcc will throw error if some timers overlap
#define _CASE_(tim, index) case ((unsigned)tim >> _CASE_SHF): return index; break
#define _CASE(i) _CASE_(TMR##i##_BASE, TIMER_INDEX(i))

// let gcc do the work, switch should be quite optimized
    switch ((unsigned)tim >> _CASE_SHF) {
#if USED_TIMERS & TIM_N(1)
        _CASE(1);
#endif
#if USED_TIMERS & TIM_N(2)
        _CASE(2);
#endif
#if USED_TIMERS & TIM_N(3)
        _CASE(3);
#endif
#if USED_TIMERS & TIM_N(4)
        _CASE(4);
#endif
#if USED_TIMERS & TIM_N(5)
        _CASE(5);
#endif
#if USED_TIMERS & TIM_N(6)
        _CASE(6);
#endif
#if USED_TIMERS & TIM_N(7)
        _CASE(7);
#endif
#if USED_TIMERS & TIM_N(8)
        _CASE(8);
#endif
#if USED_TIMERS & TIM_N(9)
        _CASE(9);
#endif
#if USED_TIMERS & TIM_N(10)
        _CASE(10);
#endif
#if USED_TIMERS & TIM_N(11)
        _CASE(11);
#endif
#if USED_TIMERS & TIM_N(12)
        _CASE(12);
#endif
#if USED_TIMERS & TIM_N(13)
        _CASE(13);
#endif
#if USED_TIMERS & TIM_N(14)
        _CASE(14);
#endif
#if USED_TIMERS & TIM_N(20)
        _CASE(20);
#endif
    default:  return ~1;  // make sure final index is out of range
    }
#undef _CASE
#undef _CASE_
}

TIM_TypeDef * const usedTimers[USED_TIMER_COUNT] = {
#define _DEF(i) TMR##i

#if USED_TIMERS & TIM_N(1)
    _DEF(1),
#endif
#if USED_TIMERS & TIM_N(2)
    _DEF(2),
#endif
#if USED_TIMERS & TIM_N(3)
    _DEF(3),
#endif
#if USED_TIMERS & TIM_N(4)
    _DEF(4),
#endif
#if USED_TIMERS & TIM_N(5)
    _DEF(5),
#endif
#if USED_TIMERS & TIM_N(6)
    _DEF(6),
#endif
#if USED_TIMERS & TIM_N(7)
    _DEF(7),
#endif
#if USED_TIMERS & TIM_N(8)
    _DEF(8),
#endif
#if USED_TIMERS & TIM_N(9)
    _DEF(9),
#endif
#if USED_TIMERS & TIM_N(10)
    _DEF(10),
#endif
#if USED_TIMERS & TIM_N(11)
    _DEF(11),
#endif
#if USED_TIMERS & TIM_N(12)
    _DEF(12),
#endif
#if USED_TIMERS & TIM_N(13)
    _DEF(13),
#endif
#if USED_TIMERS & TIM_N(14)
    _DEF(14),
#endif
#if USED_TIMERS & TIM_N(20)
    _DEF(20),
#endif
#undef _DEF
};

// Map timer index to timer number (Straight copy of usedTimers array)
const int8_t timerNumbers[USED_TIMER_COUNT] = {
#define _DEF(i) i

#if USED_TIMERS & TIM_N(1)
    _DEF(1),
#endif
#if USED_TIMERS & TIM_N(2)
    _DEF(2),
#endif
#if USED_TIMERS & TIM_N(3)
    _DEF(3),
#endif
#if USED_TIMERS & TIM_N(4)
    _DEF(4),
#endif
#if USED_TIMERS & TIM_N(5)
    _DEF(5),
#endif
#if USED_TIMERS & TIM_N(6)
    _DEF(6),
#endif
#if USED_TIMERS & TIM_N(7)
    _DEF(7),
#endif
#if USED_TIMERS & TIM_N(8)
    _DEF(8),
#endif
#if USED_TIMERS & TIM_N(9)
    _DEF(9),
#endif
#if USED_TIMERS & TIM_N(10)
    _DEF(10),
#endif
#if USED_TIMERS & TIM_N(11)
    _DEF(11),
#endif
#if USED_TIMERS & TIM_N(12)
    _DEF(12),
#endif
#if USED_TIMERS & TIM_N(13)
    _DEF(13),
#endif
#if USED_TIMERS & TIM_N(14)
    _DEF(14),
#endif
#if USED_TIMERS & TIM_N(20)
    _DEF(20),
#endif
#undef _DEF
};

int8_t timerGetNumberByIndex(uint8_t index)
{
    if (index < USED_TIMER_COUNT) {
        return timerNumbers[index];
    } else {
        return 0;
    }
}

int8_t timerGetTIMNumber(const TIM_TypeDef *tim)
{
    const uint8_t index = lookupTimerIndex(tim);

    return timerGetNumberByIndex(index);
}

static inline uint8_t lookupChannelIndex(const uint16_t channel)
{
    return channel >> 2;
}

uint8_t timerLookupChannelIndex(const uint16_t channel)
{
    return lookupChannelIndex(channel);
}

rccPeriphTag_t timerRCC(TIM_TypeDef *tim)
{
    for (int i = 0; i < HARDWARE_TIMER_DEFINITION_COUNT; i++) {
        if (timerDefinitions[i].TIMx == tim) {
            return timerDefinitions[i].rcc;
        }
    }
    return 0;
}

uint8_t timerInputIrq(TIM_TypeDef *tim)
{
    for (int i = 0; i < HARDWARE_TIMER_DEFINITION_COUNT; i++) {
        if (timerDefinitions[i].TIMx == tim) {
            return timerDefinitions[i].inputIrq;
        }
    }
    return 0;
}

void timerNVICConfigure(uint8_t irq)
{
    nvic_irq_enable(irq, NVIC_PRIORITY_BASE(NVIC_PRIO_TIMER), NVIC_PRIORITY_SUB(NVIC_PRIO_TIMER));
}

void configTimeBase(TIM_TypeDef *tim, uint16_t period, uint32_t hz)
{
    // "The counter clock frequency (CK_CNT) is equal to f CK_PSC / (PSC[15:0] + 1)."
    // Thus for 1Mhz: 72000000 / 1000000 = 72, 72 - 1 = 71 = prescaler
    tmr_base_init(tim, (period - 1) & 0xFFFF, (timerClock(tim) / hz) - 1);
    tmr_clock_source_div_set(tim, TMR_CLOCK_DIV1);
    tmr_cnt_dir_set(tim, TMR_COUNT_UP);
}

void timerReconfigureTimeBase(TIM_TypeDef *tim, uint16_t period, uint32_t hz)
{
    configTimeBase(tim, period, hz);
}

// old interface for PWM inputs. It should be replaced
void timerConfigure(const timerHardware_t *timerHardwarePtr, uint16_t period, uint32_t hz)
{
    configTimeBase(timerHardwarePtr->tim, period, hz);
    tmr_counter_enable(timerHardwarePtr->tim, TRUE);

    uint8_t irq = timerInputIrq(timerHardwarePtr->tim);
    timerNVICConfigure(irq);
    // HACK - enable second IRQ on timers that need it (TMR1/TMR8 "channel" IRQ is separate
    // from the shared "overflow" IRQ that TMR10/TMR13 alias onto -- see timer_at32f43x.c's
    // header comment for the full AT32F435/437 IRQn sharing layout).
    switch (irq) {
    case TMR1_CH_IRQn:
        timerNVICConfigure(TMR1_OVF_TMR10_IRQn);
        break;
    case TMR8_CH_IRQn:
        timerNVICConfigure(TMR8_OVF_TMR13_IRQn);
        break;
    }
}

// allocate and configure timer channel. Timer priority is set to highest priority of its channels
void timerChInit(const timerHardware_t *timHw, channelType_t type, int irqPriority, uint8_t irq)
{
    unsigned channel = timHw - TIMER_HARDWARE;
    if (channel >= TIMER_CHANNEL_COUNT) {
        return;
    }

    timerChannelInfo[channel].type = type;
    unsigned timer = lookupTimerIndex(timHw->tim);
    if (timer >= USED_TIMER_COUNT)
        return;
    if (irqPriority < timerInfo[timer].priority) {
        // it would be better to set priority in the end, but current startup sequence is not ready
        configTimeBase(usedTimers[timer], 0, 1);
        tmr_counter_enable(usedTimers[timer], TRUE);

        nvic_irq_enable(irq, NVIC_PRIORITY_BASE(irqPriority), NVIC_PRIORITY_SUB(irqPriority));

        timerInfo[timer].priority = irqPriority;
    }
}

void timerChCCHandlerInit(timerCCHandlerRec_t *self, timerCCHandlerCallback *fn)
{
    self->fn = fn;
}

void timerChOvrHandlerInit(timerOvrHandlerRec_t *self, timerOvrHandlerCallback *fn)
{
    self->fn = fn;
    self->next = NULL;
}

// update overflow callback list
// some synchronization mechanism is neccesary to avoid disturbing other channels (BASEPRI used now)
static void timerChConfig_UpdateOverflow(timerConfig_t *cfg, const TIM_TypeDef *tim) {
    timerOvrHandlerRec_t **chain = &cfg->overflowCallbackActive;
    ATOMIC_BLOCK(NVIC_PRIO_TIMER) {

        if (cfg->updateCallback) {
            *chain = cfg->updateCallback;
            chain = &cfg->updateCallback->next;
        }

        for (int i = 0; i < CC_CHANNELS_PER_TIMER; i++)
            if (cfg->overflowCallback[i]) {
                *chain = cfg->overflowCallback[i];
                chain = &cfg->overflowCallback[i]->next;
            }
        *chain = NULL;
    }
    // enable or disable IRQ
    tmr_interrupt_enable((TIM_TypeDef *)tim, TMR_OVF_INT, cfg->overflowCallbackActive ? TRUE : FALSE);
}

// config edge and overflow callback for channel. Try to avoid per-channel overflowCallback, it is a bit expensive
void timerChConfigCallbacks(const timerHardware_t *timHw, timerCCHandlerRec_t *edgeCallback, timerOvrHandlerRec_t *overflowCallback)
{
    uint8_t timerIndex = lookupTimerIndex(timHw->tim);
    if (timerIndex >= USED_TIMER_COUNT) {
        return;
    }
    uint8_t channelIndex = lookupChannelIndex(timHw->channel);
    if (edgeCallback == NULL)   // disable irq before changing callback to NULL
        tmr_interrupt_enable(timHw->tim, TIM_IT_CCx(timHw->channel), FALSE);
    // setup callback info
    timerConfig[timerIndex].edgeCallback[channelIndex] = edgeCallback;
    timerConfig[timerIndex].overflowCallback[channelIndex] = overflowCallback;
    // enable channel IRQ
    if (edgeCallback)
        tmr_interrupt_enable(timHw->tim, TIM_IT_CCx(timHw->channel), TRUE);

    timerChConfig_UpdateOverflow(&timerConfig[timerIndex], timHw->tim);
}

void timerConfigUpdateCallback(const TIM_TypeDef *tim, timerOvrHandlerRec_t *updateCallback)
{
    uint8_t timerIndex = lookupTimerIndex(tim);
    if (timerIndex >= USED_TIMER_COUNT) {
        return;
    }
    timerConfig[timerIndex].updateCallback = updateCallback;
    timerChConfig_UpdateOverflow(&timerConfig[timerIndex], tim);
}

// configure callbacks for pair of channels (1+2 or 3+4).
// Hi(2,4) and Lo(1,3) callbacks are specified, it is not important which timHw channel is used.
// This is intended for dual capture mode (each channel handles one transition)
void timerChConfigCallbacksDual(const timerHardware_t *timHw, timerCCHandlerRec_t *edgeCallbackLo, timerCCHandlerRec_t *edgeCallbackHi, timerOvrHandlerRec_t *overflowCallback)
{
    uint8_t timerIndex = lookupTimerIndex(timHw->tim);
    if (timerIndex >= USED_TIMER_COUNT) {
        return;
    }
    uint16_t chLo = timHw->channel & ~TIM_Channel_2;   // lower channel
    uint16_t chHi = timHw->channel | TIM_Channel_2;    // upper channel
    uint8_t channelIndex = lookupChannelIndex(chLo);   // get index of lower channel

    if (edgeCallbackLo == NULL)   // disable irq before changing setting callback to NULL
        tmr_interrupt_enable(timHw->tim, TIM_IT_CCx(chLo), FALSE);
    if (edgeCallbackHi == NULL)   // disable irq before changing setting callback to NULL
        tmr_interrupt_enable(timHw->tim, TIM_IT_CCx(chHi), FALSE);

    // setup callback info
    timerConfig[timerIndex].edgeCallback[channelIndex] = edgeCallbackLo;
    timerConfig[timerIndex].edgeCallback[channelIndex + 1] = edgeCallbackHi;
    timerConfig[timerIndex].overflowCallback[channelIndex] = overflowCallback;
    timerConfig[timerIndex].overflowCallback[channelIndex + 1] = NULL;

    // enable channel IRQs
    if (edgeCallbackLo) {
        tmr_flag_clear(timHw->tim, TIM_IT_CCx(chLo));
        tmr_interrupt_enable(timHw->tim, TIM_IT_CCx(chLo), TRUE);
    }
    if (edgeCallbackHi) {
        tmr_flag_clear(timHw->tim, TIM_IT_CCx(chHi));
        tmr_interrupt_enable(timHw->tim, TIM_IT_CCx(chHi), TRUE);
    }

    timerChConfig_UpdateOverflow(&timerConfig[timerIndex], timHw->tim);
}

// enable/disable IRQ for low channel in dual configuration
void timerChITConfigDualLo(const timerHardware_t *timHw, FunctionalState newState) {
    tmr_interrupt_enable(timHw->tim, TIM_IT_CCx(timHw->channel & ~TIM_Channel_2), newState ? TRUE : FALSE);
}

// enable or disable IRQ
void timerChITConfig(const timerHardware_t *timHw, FunctionalState newState)
{
    tmr_interrupt_enable(timHw->tim, TIM_IT_CCx(timHw->channel), newState ? TRUE : FALSE);
}

// clear Compare/Capture flag for channel
void timerChClearCCFlag(const timerHardware_t *timHw)
{
    tmr_flag_clear(timHw->tim, TIM_IT_CCx(timHw->channel));
}

// configure timer channel GPIO mode
void timerChConfigGPIO(const timerHardware_t* timHw, ioConfig_t mode)
{
    IOInit(IOGetByTag(timHw->tag), OWNER_TIMER, 0);
    IOConfigGPIO(IOGetByTag(timHw->tag), mode);
}

// calculate input filter constant
// TODO - we should probably setup DTS to higher value to allow reasonable input filtering
//   - notice that prescaler[0] does use DTS for sampling - the sequence won't be monotonous anymore
static unsigned getFilter(unsigned ticks)
{
    static const unsigned ftab[16] = {
        1*1,                 // fDTS !
        1*2, 1*4, 1*8,       // fCK_INT
        2*6, 2*8,            // fDTS/2
        4*6, 4*8,
        8*6, 8*8,
        16*5, 16*6, 16*8,
        32*5, 32*6, 32*8
    };
    for (unsigned i = 1; i < ARRAYLEN(ftab); i++)
        if (ftab[i] > ticks)
            return i - 1;
    return 0x0f;
}

// Configure input capture
void timerChConfigIC(const timerHardware_t *timHw, bool polarityRising, unsigned inputFilterTicks)
{
    tmr_input_config_type tmr_icInitStructure;

    tmr_icInitStructure.input_channel_select = AT_CH_SELECT(timHw->channel);
    tmr_icInitStructure.input_polarity_select = polarityRising ? TMR_INPUT_RISING_EDGE : TMR_INPUT_FALLING_EDGE;
    tmr_icInitStructure.input_mapped_select = TMR_CC_CHANNEL_MAPPED_DIRECT;
    tmr_icInitStructure.input_filter_value = getFilter(inputFilterTicks);

    tmr_input_channel_init(timHw->tim, &tmr_icInitStructure, TMR_CHANNEL_INPUT_DIV_1);
}

// configure dual channel input channel for capture
// polarity is for Low channel (capture order is always Lo - Hi)
void timerChConfigICDual(const timerHardware_t *timHw, bool polarityRising, unsigned inputFilterTicks)
{
    tmr_input_config_type tmr_icInitStructure;
    bool directRising = (timHw->channel & TIM_Channel_2) ? !polarityRising : polarityRising;

    // configure direct channel
    tmr_icInitStructure.input_channel_select = AT_CH_SELECT(timHw->channel);
    tmr_icInitStructure.input_polarity_select = directRising ? TMR_INPUT_RISING_EDGE : TMR_INPUT_FALLING_EDGE;
    tmr_icInitStructure.input_mapped_select = TMR_CC_CHANNEL_MAPPED_DIRECT;
    tmr_icInitStructure.input_filter_value = getFilter(inputFilterTicks);
    tmr_input_channel_init(timHw->tim, &tmr_icInitStructure, TMR_CHANNEL_INPUT_DIV_1);

    // configure indirect channel
    tmr_icInitStructure.input_channel_select = AT_CH_SELECT(timHw->channel ^ TIM_Channel_2);   // get opposite channel no
    tmr_icInitStructure.input_polarity_select = directRising ? TMR_INPUT_FALLING_EDGE : TMR_INPUT_RISING_EDGE;
    tmr_icInitStructure.input_mapped_select = TMR_CC_CHANNEL_MAPPED_INDIRECT;
    tmr_input_channel_init(timHw->tim, &tmr_icInitStructure, TMR_CHANNEL_INPUT_DIV_1);
}

void timerChICPolarity(const timerHardware_t *timHw, bool polarityRising)
{
    // AT-BSP's cctrl register (CCER equivalent) has the identical bit layout STM32 uses:
    // c1en/c1p/c1cen/c1cp at bits 0-3, c2xx at 4-7, c3xx at 8-11, c4en/c4p at 12-13 (no
    // complementary bits for channel 4) -- so the same channel-value-as-shift-amount trick
    // used by the original STM32 code (channel is 0/4/8/12) applies unchanged here.
#define TMR_CCTRL_CxP (1u << 1) // c1p bit position within a channel's 4-bit group
    timCCER_t tmpccer = timHw->tim->cctrl;
    tmpccer &= ~(TMR_CCTRL_CxP << timHw->channel);
    tmpccer |= (polarityRising ? 0u : TMR_CCTRL_CxP) << timHw->channel;
    timHw->tim->cctrl = tmpccer;
#undef TMR_CCTRL_CxP
}

volatile timCCR_t* timerChCCRHi(const timerHardware_t *timHw)
{
    return (volatile timCCR_t*)((volatile char*)&timHw->tim->c1dt + (timHw->channel | TIM_Channel_2));
}

volatile timCCR_t* timerChCCRLo(const timerHardware_t *timHw)
{
    return (volatile timCCR_t*)((volatile char*)&timHw->tim->c1dt + (timHw->channel & ~TIM_Channel_2));
}

volatile timCCR_t* timerChCCR(const timerHardware_t *timHw)
{
    return (volatile timCCR_t*)((volatile char*)&timHw->tim->c1dt + timHw->channel);
}

void timerChConfigOC(const timerHardware_t* timHw, bool outEnable, bool stateHigh)
{
    tmr_output_config_type tmr_ocInitStructure;

    tmr_output_default_para_init(&tmr_ocInitStructure);
    if (outEnable) {
        // AT-BSP's TMR_OUTPUT_CONTROL_FORCE_LOW forces OCxREF low ahead of the
        // polarity/inversion stage, matching STM32's TIM_OCMode_Inactive semantics
        // (channel enabled, output driven to a static level controlled purely by polarity).
        tmr_ocInitStructure.oc_mode = TMR_OUTPUT_CONTROL_FORCE_LOW;
        tmr_ocInitStructure.oc_output_state = TRUE;
        if (timHw->output & TIMER_OUTPUT_INVERTED) {
            stateHigh = !stateHigh;
        }
        tmr_ocInitStructure.oc_polarity = stateHigh ? TMR_OUTPUT_ACTIVE_HIGH : TMR_OUTPUT_ACTIVE_LOW;
    } else {
        // TMR_OUTPUT_CONTROL_OFF has no effect on the output pin at all, matching STM32's
        // TIM_OCMode_Timing (channel used purely for capture/measurement, not driving a pin).
        tmr_ocInitStructure.oc_mode = TMR_OUTPUT_CONTROL_OFF;
        tmr_ocInitStructure.oc_output_state = FALSE;
    }

    tmr_output_channel_config(timHw->tim, AT_CH_SELECT(timHw->channel), &tmr_ocInitStructure);
    tmr_output_channel_buffer_enable(timHw->tim, AT_CH_SELECT(timHw->channel), FALSE);
}

static void timCCxHandler(TIM_TypeDef *tim, timerConfig_t *timerConfig)
{
    uint16_t capture;
    unsigned tim_status;
    // iden (interrupt/dma enable register) and ists (interrupt status register) share the
    // same per-source bit positions (ovfXX=bit0, c1XX=bit1..c4XX=bit4, hallXX=bit5,
    // trgXX/tXX=bit6, brkXX=bit7), exactly like STM32's DIER/SR pairing.
    tim_status = tim->ists & tim->iden;
#if 1
    while (tim_status) {
        // flags will be cleared by reading CCR in dual capture, make sure we call handler correctly
        // current order is highest bit first. Code should not rely on specific order (it will introduce race conditions anyway)
        unsigned bit = __builtin_clz(tim_status);
        unsigned mask = ~(0x80000000 >> bit);
        tim->ists = mask;
        tim_status &= mask;
        switch (bit) {
            case __builtin_clz(TMR_OVF_INT): {

                if (timerConfig->forcedOverflowTimerValue != 0) {
                    capture = timerConfig->forcedOverflowTimerValue - 1;
                    timerConfig->forcedOverflowTimerValue = 0;
                } else {
                    capture = tim->pr;
                }

                timerOvrHandlerRec_t *cb = timerConfig->overflowCallbackActive;
                while (cb) {
                    cb->fn(cb, capture);
                    cb = cb->next;
                }
                break;
            }
            case __builtin_clz(TMR_C1_INT):
                timerConfig->edgeCallback[0]->fn(timerConfig->edgeCallback[0], tim->c1dt);
                break;
            case __builtin_clz(TMR_C2_INT):
                timerConfig->edgeCallback[1]->fn(timerConfig->edgeCallback[1], tim->c2dt);
                break;
            case __builtin_clz(TMR_C3_INT):
                timerConfig->edgeCallback[2]->fn(timerConfig->edgeCallback[2], tim->c3dt);
                break;
            case __builtin_clz(TMR_C4_INT):
                timerConfig->edgeCallback[3]->fn(timerConfig->edgeCallback[3], tim->c4dt);
                break;
        }
    }
#else
    if (tim_status & (int)TMR_OVF_INT) {
        tim->ists = ~TMR_OVF_INT;
        capture = tim->pr;
        timerOvrHandlerRec_t *cb = timerConfig->overflowCallbackActive;
        while (cb) {
            cb->fn(cb, capture);
            cb = cb->next;
        }
    }
    if (tim_status & (int)TMR_C1_INT) {
        tim->ists = ~TMR_C1_INT;
        timerConfig->edgeCallback[0]->fn(timerConfig->edgeCallback[0], tim->c1dt);
    }
    if (tim_status & (int)TMR_C2_INT) {
        tim->ists = ~TMR_C2_INT;
        timerConfig->edgeCallback[1]->fn(timerConfig->edgeCallback[1], tim->c2dt);
    }
    if (tim_status & (int)TMR_C3_INT) {
        tim->ists = ~TMR_C3_INT;
        timerConfig->edgeCallback[2]->fn(timerConfig->edgeCallback[2], tim->c3dt);
    }
    if (tim_status & (int)TMR_C4_INT) {
        tim->ists = ~TMR_C4_INT;
        timerConfig->edgeCallback[3]->fn(timerConfig->edgeCallback[3], tim->c4dt);
    }
#endif
}

static inline void timUpdateHandler(TIM_TypeDef *tim, timerConfig_t *timerConfig)
{
    uint16_t capture;
    unsigned tim_status;
    tim_status = tim->ists & tim->iden;
    while (tim_status) {
        // flags will be cleared by reading CCR in dual capture, make sure we call handler correctly
        // currrent order is highest bit first. Code should not rely on specific order (it will introduce race conditions anyway)
        unsigned bit = __builtin_clz(tim_status);
        unsigned mask = ~(0x80000000 >> bit);
        tim->ists = mask;
        tim_status &= mask;
        switch (bit) {
            case __builtin_clz(TMR_OVF_INT): {

                if (timerConfig->forcedOverflowTimerValue != 0) {
                    capture = timerConfig->forcedOverflowTimerValue - 1;
                    timerConfig->forcedOverflowTimerValue = 0;
                } else {
                    capture = tim->pr;
                }

                timerOvrHandlerRec_t *cb = timerConfig->overflowCallbackActive;
                while (cb) {
                    cb->fn(cb, capture);
                    cb = cb->next;
                }
                break;
            }
        }
    }
}

// handler for shared interrupts when both timers need to check status bits
#define _TIM_IRQ_HANDLER2(name, i, j)                                   \
    void name(void)                                                     \
    {                                                                   \
        timCCxHandler(TMR ## i, &timerConfig[TIMER_INDEX(i)]);          \
        timCCxHandler(TMR ## j, &timerConfig[TIMER_INDEX(j)]);          \
    } struct dummy

#define _TIM_IRQ_HANDLER(name, i)                                       \
    void name(void)                                                     \
    {                                                                   \
        timCCxHandler(TMR ## i, &timerConfig[TIMER_INDEX(i)]);          \
    } struct dummy

#define _TIM_IRQ_HANDLER_UPDATE_ONLY(name, i)                           \
    void name(void)                                                     \
    {                                                                   \
        timUpdateHandler(TMR ## i, &timerConfig[TIMER_INDEX(i)]);       \
    } struct dummy

#if USED_TIMERS & TIM_N(1)
_TIM_IRQ_HANDLER(TMR1_CH_IRQHandler, 1);
#endif
#if USED_TIMERS & TIM_N(2)
_TIM_IRQ_HANDLER(TMR2_GLOBAL_IRQHandler, 2);
#endif
#if USED_TIMERS & TIM_N(3)
_TIM_IRQ_HANDLER(TMR3_GLOBAL_IRQHandler, 3);
#endif
#if USED_TIMERS & TIM_N(4)
_TIM_IRQ_HANDLER(TMR4_GLOBAL_IRQHandler, 4);
#endif
#if USED_TIMERS & TIM_N(5)
_TIM_IRQ_HANDLER(TMR5_GLOBAL_IRQHandler, 5);
#endif

#if USED_TIMERS & TIM_N(6)
// TMR6 shares its IRQ vector with DAC on AT32F435/437 (TMR6_DAC_GLOBAL_IRQHandler).
_TIM_IRQ_HANDLER_UPDATE_ONLY(TMR6_DAC_GLOBAL_IRQHandler, 6);
#endif

#if USED_TIMERS & TIM_N(7)
// NOTE: if/when AT32 USB VCP support is added, check whether it shares TMR7 the way the
// STM32F4/G4/H7 VCP HAL drivers share TIM7 (see the equivalent guard in drivers/timer.c) --
// no such conflict is known today because AT32 VCP has not been ported yet.
_TIM_IRQ_HANDLER_UPDATE_ONLY(TMR7_GLOBAL_IRQHandler, 7);
#endif

#if USED_TIMERS & TIM_N(8)
_TIM_IRQ_HANDLER(TMR8_CH_IRQHandler, 8);
#endif
#if USED_TIMERS & TIM_N(9)
_TIM_IRQ_HANDLER(TMR1_BRK_TMR9_IRQHandler, 9);
#endif
#if USED_TIMERS & TIM_N(10)
#  if USED_TIMERS & TIM_N(1)
_TIM_IRQ_HANDLER2(TMR1_OVF_TMR10_IRQHandler, 1, 10);  // both timers are in use
#  else
_TIM_IRQ_HANDLER(TMR1_OVF_TMR10_IRQHandler, 10);      // timer1 is not used
#  endif
#endif
#if USED_TIMERS & TIM_N(11)
_TIM_IRQ_HANDLER(TMR1_TRG_HALL_TMR11_IRQHandler, 11);
#endif
#if USED_TIMERS & TIM_N(12)
_TIM_IRQ_HANDLER(TMR8_BRK_TMR12_IRQHandler, 12);
#endif
#if USED_TIMERS & TIM_N(13)
#  if USED_TIMERS & TIM_N(8)
_TIM_IRQ_HANDLER2(TMR8_OVF_TMR13_IRQHandler, 8, 13);  // both timers are in use
#  else
_TIM_IRQ_HANDLER(TMR8_OVF_TMR13_IRQHandler, 13);      // timer8 is not used
#  endif
#endif
#if USED_TIMERS & TIM_N(14)
_TIM_IRQ_HANDLER(TMR8_TRG_HALL_TMR14_IRQHandler, 14);
#endif
#if USED_TIMERS & TIM_N(20)
_TIM_IRQ_HANDLER(TMR20_CH_IRQHandler, 20);
#endif

void timerInit(void)
{
    memset(timerConfig, 0, sizeof(timerConfig));

    /* enable the timer peripherals */
    for (unsigned i = 0; i < TIMER_CHANNEL_COUNT; i++) {
        RCC_ClockCmd(timerRCC(TIMER_HARDWARE[i].tim), ENABLE);
    }

    // initialize timer channel structures
    for (unsigned i = 0; i < TIMER_CHANNEL_COUNT; i++) {
        timerChannelInfo[i].type = TYPE_FREE;
    }

    for (unsigned i = 0; i < USED_TIMER_COUNT; i++) {
        timerInfo[i].priority = ~0;
    }
}

// finish configuring timers after allocation phase
// start timers
void timerStart(void)
{
    // Not yet implemented -- matches the STM32 non-HAL (timer.c) precedent, whose
    // timerStart() body is also a no-op (dead code guarded out with #if 0).
}

/**
 * Force an overflow for a given timer.
 * Saves the current value of the counter in the relevant timerConfig's forcedOverflowTimerValue variable.
 * @param TIM_Typedef *tim The timer to overflow
 * @return void
 **/
void timerForceOverflow(TIM_TypeDef *tim)
{
    uint8_t timerIndex = lookupTimerIndex((const TIM_TypeDef *)tim);

    ATOMIC_BLOCK(NVIC_PRIO_TIMER) {
        // Save the current count so that PPM reading will work on the same timer that was forced to overflow
        timerConfig[timerIndex].forcedOverflowTimerValue = tim->cval + 1;

        // Force an overflow by setting the software overflow-trigger bit
        tim->swevt_bit.ovfswtr = 1;
    }
}

void timerOCInit(TIM_TypeDef *tim, uint8_t channel, TIM_OCInitTypeDef *init)
{
    tmr_output_channel_config(tim, AT_CH_SELECT(channel), init);
}

void timerOCPreloadConfig(TIM_TypeDef *tim, uint8_t channel, uint16_t preload)
{
    tmr_output_channel_buffer_enable(tim, AT_CH_SELECT(channel), preload ? TRUE : FALSE);
}

uint16_t timerDmaSource(uint8_t channel)
{
    switch (channel) {
    case TIM_Channel_1:
        return TMR_C1_DMA_REQUEST;
    case TIM_Channel_2:
        return TMR_C2_DMA_REQUEST;
    case TIM_Channel_3:
        return TMR_C3_DMA_REQUEST;
    case TIM_Channel_4:
        return TMR_C4_DMA_REQUEST;
    }
    return 0;
}

uint16_t timerGetPrescalerByDesiredMhz(TIM_TypeDef *tim, uint16_t mhz)
{
    return timerGetPrescalerByDesiredHertz(tim, MHZ_TO_HZ(mhz));
}

uint16_t timerGetPeriodByPrescaler(TIM_TypeDef *tim, uint16_t prescaler, uint32_t hz)
{
    return (uint16_t)((timerClock(tim) / (prescaler + 1)) / hz);
}

uint16_t timerGetPrescalerByDesiredHertz(TIM_TypeDef *tim, uint32_t hz)
{
    // protection here for desired hertz > SystemCoreClock???
    if (hz > timerClock(tim)) {
        return 0;
    }
    return (uint16_t)((timerClock(tim) + hz / 2 ) / hz) - 1;
}
#endif
