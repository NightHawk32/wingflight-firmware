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
 * Adapted from Artery's at32f435_437_clock.c (AT-BSP template), as used by
 * betaflight/src/platform/AT32/startup/at32f435_437_clock.c.
 *
 * Configures: sclk = (hext * pll_ns) / (pll_ms * pll_fr), ahbclk = sclk / 1,
 * apb1clk = sclk / 2, apb2clk = sclk / 2, usbclk = pll / 6.
 *
 * KNOWN LIMITATION (see AT32F435_TODO.md): the PLL multiplier/divider values below
 * are only correct for an 8MHz HEXT crystal (sclk = 288MHz). Unlike the STM32 targets,
 * this does not yet compute PLL parameters dynamically for other HSE_VALUE settings.
 * Building for a board with a different crystal will require extending this function.
 */

#include "at32f435_437_clock.h"
#include "platform.h"

#if HSE_VALUE != 8000000
#error "AT32F435 system_clock_config() only supports an 8MHz HEXT crystal today - see AT32F435_TODO.md"
#endif

void system_clock_config(void)
{
    // Enable PWC clock and set LDO output voltage for 288MHz operation
    crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
    pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);

    flash_clock_divider_set(FLASH_CLOCK_DIV_3);

    crm_reset();

    crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
    while (crm_hext_stable_wait() == ERROR) {
        // wait for HEXT to stabilize
    }

    crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK, TRUE);
    while (crm_flag_get(CRM_HICK_STABLE_FLAG) != SET) {
        // wait for HICK to stabilize
    }

    // sclk = (8MHz * 72) / (1 * 2) = 288MHz
    crm_pll_config(CRM_PLL_SOURCE_HEXT, 72, 1, CRM_PLL_FR_2);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
    while (crm_flag_get(CRM_PLL_STABLE_FLAG) != SET) {
        // wait for PLL to lock
    }

    crm_ahb_div_set(CRM_AHB_DIV_1);
    crm_apb2_div_set(CRM_APB2_DIV_2);
    crm_apb1_div_set(CRM_APB1_DIV_2);

    // Auto-step mode ramps sclk gradually to avoid overshoot while switching to PLL
    crm_auto_step_mode_enable(TRUE);
    crm_sysclk_switch(CRM_SCLK_PLL);
    while (crm_sysclk_switch_status_get() != CRM_SCLK_PLL) {
        // wait for the switch to PLL to take effect
    }
    crm_auto_step_mode_enable(FALSE);

    crm_usb_clock_div_set(CRM_USB_DIV_6); // 288MHz / 6 = 48MHz
    crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_PLL);

    system_core_clock_update();
}
