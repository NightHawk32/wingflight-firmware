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

#include "platform.h"

#ifdef USE_TIMER

#include "common/utils.h"

#include "drivers/dma.h"
#include "drivers/io.h"
#include "drivers/timer_def.h"

#include "rcc.h"
#include "timer.h"

// AT32F435/437 IRQn naming: TMR1/TMR8/TMR20 have a dedicated "channel" (capture/compare)
// IRQ; TMR9/10/11 alias TMR1's BRK/OVF/TRG_HALL IRQs and TMR12/13/14 alias TMR8's, exactly
// mirroring the STM32F4 TIM1/TIM8-BRK/UP/TRG-sharing convention used in timer_stm32f4xx.c.
const timerDef_t timerDefinitions[HARDWARE_TIMER_DEFINITION_COUNT] = {
    { .TIMx = TMR1,  .rcc = RCC_APB2(TMR1),  .inputIrq = TMR1_CH_IRQn },
    { .TIMx = TMR2,  .rcc = RCC_APB1(TMR2),  .inputIrq = TMR2_GLOBAL_IRQn },
    { .TIMx = TMR3,  .rcc = RCC_APB1(TMR3),  .inputIrq = TMR3_GLOBAL_IRQn },
    { .TIMx = TMR4,  .rcc = RCC_APB1(TMR4),  .inputIrq = TMR4_GLOBAL_IRQn },
    { .TIMx = TMR5,  .rcc = RCC_APB1(TMR5),  .inputIrq = TMR5_GLOBAL_IRQn },
    { .TIMx = TMR6,  .rcc = RCC_APB1(TMR6),  .inputIrq = 0 },
    { .TIMx = TMR7,  .rcc = RCC_APB1(TMR7),  .inputIrq = 0 },
    { .TIMx = TMR8,  .rcc = RCC_APB2(TMR8),  .inputIrq = TMR8_CH_IRQn },
    { .TIMx = TMR9,  .rcc = RCC_APB2(TMR9),  .inputIrq = TMR1_BRK_TMR9_IRQn },
    { .TIMx = TMR10, .rcc = RCC_APB2(TMR10), .inputIrq = TMR1_OVF_TMR10_IRQn },
    { .TIMx = TMR11, .rcc = RCC_APB2(TMR11), .inputIrq = TMR1_TRG_HALL_TMR11_IRQn },
    { .TIMx = TMR12, .rcc = RCC_APB1(TMR12), .inputIrq = TMR8_BRK_TMR12_IRQn },
    { .TIMx = TMR13, .rcc = RCC_APB1(TMR13), .inputIrq = TMR8_OVF_TMR13_IRQn },
    { .TIMx = TMR14, .rcc = RCC_APB1(TMR14), .inputIrq = TMR8_TRG_HALL_TMR14_IRQn },
    { .TIMx = TMR20, .rcc = RCC_APB2(TMR20), .inputIrq = TMR20_CH_IRQn },
};

#if defined(USE_TIMER_MGMT)
const timerHardware_t fullTimerHardware[FULL_TIMER_CHANNEL_COUNT] = {
    // Auto-generated from 'timer_def.h', pin/AF data ported from betaflight's own
    // src/platform/AT32/timer_at32f43x.c (real, verified AT32F435/437 hardware data).
//PORTA
    DEF_TIM(TMR2,  CH1,  PA0,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH2,  PA1,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH3,  PA2,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH4,  PA3,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH1,  PA5,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH1,  PA15, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR5,  CH1,  PA0,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR5,  CH2,  PA1,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR5,  CH3,  PA2,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR5,  CH4,  PA3,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR3,  CH1,  PA6,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR3,  CH2,  PA7,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR8,  CH1N, PA5,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR8,  CH1N, PA7,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH1N, PA7,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH1,  PA8,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH2,  PA9,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH3,  PA10, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH4,  PA11, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR9,  CH1,  PA2,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR9,  CH2,  PA3,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR13, CH1,  PA6,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR14, CH1,  PA7,  TIM_USE_ANY, 0, 0, 0),

//PORTB MUX1
    DEF_TIM(TMR1,  CH2N, PB0,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH3N, PB1,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH4,  PB2,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH2,  PB3,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH1,  PB8,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH2,  PB9,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH3,  PB10, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR2,  CH4,  PB11, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH1N, PB13, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH2N, PB14, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH3N, PB15, TIM_USE_ANY, 0, 0, 0),

//PORTB MUX2
    DEF_TIM(TMR3,  CH3,  PB0,  TIM_USE_ANY, 0, 0,  0),
    DEF_TIM(TMR3,  CH4,  PB1,  TIM_USE_ANY, 0, 0,  0),
    DEF_TIM(TMR20, CH1,  PB2,  TIM_USE_ANY, 0, 0,  0),
    DEF_TIM(TMR3,  CH1,  PB4,  TIM_USE_ANY, 0, 0,  0),
    DEF_TIM(TMR3,  CH2,  PB5,  TIM_USE_ANY, 0, 0,  0),
    DEF_TIM(TMR4,  CH1,  PB6,  TIM_USE_ANY, 0, 13, 9),
    DEF_TIM(TMR4,  CH2,  PB7,  TIM_USE_ANY, 0, 12, 9),
    DEF_TIM(TMR4,  CH3,  PB8,  TIM_USE_ANY, 0, 11, 9),
    DEF_TIM(TMR4,  CH4,  PB9,  TIM_USE_ANY, 0, 10, 9),
    DEF_TIM(TMR5,  CH4,  PB11, TIM_USE_ANY, 0, 0,  0),
    DEF_TIM(TMR5,  CH1,  PB12, TIM_USE_ANY, 0, 0,  0),

//PORTB MUX3
    DEF_TIM(TMR8,  CH2N, PB0,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR8,  CH3N, PB1,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR8,  CH2N, PB14, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR8,  CH3N, PB15, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR10, CH1,  PB8,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR11, CH1,  PB9,  TIM_USE_ANY, 0, 0, 0),

//PORTB MUX9
    DEF_TIM(TMR12, CH1,  PB14, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR12, CH2,  PB15, TIM_USE_ANY, 0, 0, 0),

//PORTC MUX2
    DEF_TIM(TMR20, CH2,  PC2,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR3,  CH1,  PC6,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR3,  CH2,  PC7,  TIM_USE_ANY, 0, 0, 12),
    DEF_TIM(TMR3,  CH3,  PC8,  TIM_USE_ANY, 0, 0, 12),
    DEF_TIM(TMR3,  CH4,  PC9,  TIM_USE_ANY, 0, 0, 12),
    DEF_TIM(TMR5,  CH2,  PC10, TIM_USE_ANY, 0, 0, 12),
    DEF_TIM(TMR5,  CH3,  PC11, TIM_USE_ANY, 0, 0, 0),

//PORTC MUX3
    DEF_TIM(TMR9,  CH1,  PC4,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR9,  CH2,  PC5,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR8,  CH1,  PC6,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR8,  CH2,  PC7,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR8,  CH3,  PC8,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR8,  CH4,  PC9,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR11, CH1N, PC12, TIM_USE_ANY, 0, 0, 0),

//PORTD MUX2
    DEF_TIM(TMR4,  CH1,  PD12, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR4,  CH2,  PD13, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR4,  CH3,  PD14, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR4,  CH4,  PD15, TIM_USE_ANY, 0, 0, 0),

//PORTE MUX1
    DEF_TIM(TMR1,  CH2N, PE1,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH1N, PE8,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH1,  PE9,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH2N, PE10, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH2,  PE11, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH3N, PE12, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH3,  PE13, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR1,  CH4,  PE14, TIM_USE_ANY, 0, 0, 0),

//PORTE MUX2
    DEF_TIM(TMR3,  CH1,  PE3,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR3,  CH2,  PE4,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR3,  CH3,  PE5,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR3,  CH4,  PE6,  TIM_USE_ANY, 0, 0, 0),

//PORTE MUX3
    DEF_TIM(TMR9,  CH1,  PE5,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR9,  CH2,  PE6,  TIM_USE_ANY, 0, 0, 0),

//PORTE MUX6
    DEF_TIM(TMR20, CH4,  PE1,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH1,  PE2,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH2,  PE3,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH1N, PE4,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH2N, PE5,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH3N, PE6,  TIM_USE_ANY, 0, 0, 0),

//PORTF MUX2
    DEF_TIM(TMR20, CH3,  PF2,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH4,  PF3,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH1N, PF4,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH2N, PF5,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH4,  PF6,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH1,  PF12, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH2,  PF13, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH3,  PF14, TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH4,  PF15, TIM_USE_ANY, 0, 0, 0),

//PORTF MUX3
    DEF_TIM(TMR10, CH1,  PF6,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR11, CH1,  PF7,  TIM_USE_ANY, 0, 0, 0),

//PORTF MUX9
    DEF_TIM(TMR13, CH1,  PF8,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR14, CH1,  PF9,  TIM_USE_ANY, 0, 0, 0),

//PORTG MUX2
    DEF_TIM(TMR20, CH1N, PG0,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH2N, PG1,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR20, CH3N, PG2,  TIM_USE_ANY, 0, 0, 0),

//PORTH MUX2
    DEF_TIM(TMR5,  CH1,  PH2,  TIM_USE_ANY, 0, 0, 0),
    DEF_TIM(TMR5,  CH2,  PH3,  TIM_USE_ANY, 0, 0, 0),
};
#endif

// AT32F435/437 timer clock source: TMR1/8/9/10/11/20 are on APB2, all others (TMR2-7,
// TMR12-14) are on APB1. Verified against betaflight's timerClockFromInstance() reference
// (src/platform/AT32/timer_at32bsp.c) and the vendor CRM header (at32f435_437_crm.h):
// crm_clocks_freq_type provides apb1_freq/apb2_freq directly (already doubled by the
// vendor driver when the respective APBx prescaler divides the AHB clock), and the timer
// clock is further doubled versus the peripheral clock whenever the APBx prescaler is >1
// (CRM->cfg_bit.apb1div/apb2div >= 4, i.e. divide-by-2-or-more), mirroring the standard
// STM32 "timer clock = 2x pclk when APB prescaler != 1" rule.
uint32_t timerClock(TIM_TypeDef *tim)
{
    crm_clocks_freq_type clocks;
    crm_clocks_freq_get(&clocks);

    if (tim == TMR1 || tim == TMR8 || tim == TMR9 || tim == TMR10 || tim == TMR11 || tim == TMR20) {
        uint32_t pclk = clocks.apb2_freq;
        if (CRM->cfg_bit.apb2div >= 4) {
            pclk *= 2;
        }
        return pclk;
    } else {
        uint32_t pclk = clocks.apb1_freq;
        if (CRM->cfg_bit.apb1div >= 4) {
            pclk *= 2;
        }
        return pclk;
    }
}
#endif
