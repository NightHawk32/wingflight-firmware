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
 * Adapted from Artery's AT32F435/437 CMSIS system template (AT-BSP), as used by
 * betaflight/src/platform/AT32/startup/system_at32f435_437.c. Provides SystemInit()
 * (called from the startup .s file before main) and system_core_clock_update()
 * (recomputes system_core_clock from the live CRM registers).
 *
 * Actual HSE/PLL bring-up (not just the post-reset default HICK state) is done by
 * system_clock_config() in at32f435_437_clock.c, called from systemInit() in
 * drivers/system_at32f43x.c.
 */

#include "at32f435_437.h"
#include "platform.h"

#define VECT_TAB_OFFSET 0x0 // Vector table base offset field, must be a multiple of 0x200

unsigned int system_core_clock = HICK_VALUE; // Core clock (HCLK), see system_core_clock_update()

void SystemInit(void)
{
#if defined(__FPU_USED) && (__FPU_USED == 1U)
    SCB->CPACR |= ((3U << 10U * 2U) |  // set cp10 full access
                   (3U << 11U * 2U));  // set cp11 full access
#endif

    // Reset the CRM clock configuration to the default reset state (for debug purposes)

    CRM->ctrl_bit.hicken = TRUE;
    while (CRM->ctrl_bit.hickstbl != SET) {
        // wait for HICK to stabilize
    }

    CRM->cfg_bit.sclksel = CRM_SCLK_HICK;
    while (CRM->cfg_bit.sclksts != CRM_SCLK_HICK) {
        // wait for the switch to HICK to take effect
    }

    CRM->cfg = 0;                    // ahbdiv, apb1div, apb2div, adcdiv, clkout, sclk switch
    CRM->ctrl &= ~(0x010D0000U);      // hexten, hextbyps, cfden, pllen
    CRM->pllcfg = 0x00033002U;        // pllms, pllns, pllfr, pllrcs
    CRM->misc1 = 0;                   // clkout[3], usbbufs, hickdiv, clkoutdiv
    CRM->clkint = 0x009F0000U;        // disable all CRM interrupts, clear pending flags

#ifdef VECT_TAB_SRAM
    SCB->VTOR = SRAM_BASE | VECT_TAB_OFFSET;
#else
    SCB->VTOR = FLASH_BASE | VECT_TAB_OFFSET;
#endif
}

void system_core_clock_update(void)
{
    static const uint8_t sys_ahb_div_table[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9 };
    static const uint8_t pll_fr_table[6] = { 1, 2, 4, 8, 16, 32 };

    crm_sclk_type sclk_source = crm_sysclk_switch_status_get();

    switch (sclk_source) {
    case CRM_SCLK_HICK:
        if (CRM->misc1_bit.hick_to_sclk && CRM->misc1_bit.hickdiv) {
            system_core_clock = HICK_VALUE * 6;
        } else {
            system_core_clock = HICK_VALUE;
        }
        break;

    case CRM_SCLK_HEXT:
        system_core_clock = HEXT_VALUE;
        break;

    case CRM_SCLK_PLL: {
        uint32_t pll_clock_source = CRM->pllcfg_bit.pllrcs;
        uint32_t pll_ns = CRM->pllcfg_bit.pllns;
        uint32_t pll_ms = CRM->pllcfg_bit.pllms;
        uint32_t pll_fr = pll_fr_table[CRM->pllcfg_bit.pllfr];
        uint32_t pllrcsfreq = (pll_clock_source == CRM_PLL_SOURCE_HICK) ? HICK_VALUE : HEXT_VALUE;

        system_core_clock = (pllrcsfreq * pll_ns) / (pll_ms * pll_fr);
        break;
    }

    default:
        system_core_clock = HICK_VALUE;
        break;
    }

    uint32_t div_value = sys_ahb_div_table[CRM->cfg_bit.ahbdiv];
    system_core_clock = system_core_clock >> div_value;
}
