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
 * Wingflight's AT-BSP module-enable/include header, adapted from Artery's
 * at32f435_437_conf_template.h (see lib/main/AT32F43x/cmsis/.../at32f435_437_conf_template.h).
 * HEXT_VALUE is overridden by the build via -DHSE_VALUE=... (see make/mcu/AT32F4.mk),
 * matching the pattern already used for HSE_VALUE on the STM32 targets.
 */

#pragma once

#ifndef HEXT_VALUE
#define HEXT_VALUE ((uint32_t)HSE_VALUE)
#endif

#define HEXT_STARTUP_TIMEOUT ((uint16_t)0x3000)
#define HICK_VALUE           ((uint32_t)8000000)

#define CRM_MODULE_ENABLED
#define TMR_MODULE_ENABLED
#define ERTC_MODULE_ENABLED
#define GPIO_MODULE_ENABLED
#define I2C_MODULE_ENABLED
#define USART_MODULE_ENABLED
#define PWC_MODULE_ENABLED
#define ADC_MODULE_ENABLED
#define SPI_MODULE_ENABLED
#define EDMA_MODULE_ENABLED
#define DMA_MODULE_ENABLED
#define DEBUG_MODULE_ENABLED
#define FLASH_MODULE_ENABLED
#define CRC_MODULE_ENABLED
#define WWDT_MODULE_ENABLED
#define WDT_MODULE_ENABLED
#define EXINT_MODULE_ENABLED
#define SDIO_MODULE_ENABLED
#define XMC_MODULE_ENABLED
#define USB_MODULE_ENABLED
#define ACC_MODULE_ENABLED
#define MISC_MODULE_ENABLED
#define SCFG_MODULE_ENABLED

#ifdef CRM_MODULE_ENABLED
#include "at32f435_437_crm.h"
#endif
#ifdef TMR_MODULE_ENABLED
#include "at32f435_437_tmr.h"
#endif
#ifdef ERTC_MODULE_ENABLED
#include "at32f435_437_ertc.h"
#endif
#ifdef GPIO_MODULE_ENABLED
#include "at32f435_437_gpio.h"
#endif
#ifdef I2C_MODULE_ENABLED
#include "at32f435_437_i2c.h"
#endif
#ifdef USART_MODULE_ENABLED
#include "at32f435_437_usart.h"
#endif
#ifdef PWC_MODULE_ENABLED
#include "at32f435_437_pwc.h"
#endif
#ifdef ADC_MODULE_ENABLED
#include "at32f435_437_adc.h"
#endif
#ifdef SPI_MODULE_ENABLED
#include "at32f435_437_spi.h"
#endif
#ifdef DMA_MODULE_ENABLED
#include "at32f435_437_dma.h"
#endif
#ifdef DEBUG_MODULE_ENABLED
#include "at32f435_437_debug.h"
#endif
#ifdef FLASH_MODULE_ENABLED
#include "at32f435_437_flash.h"
#endif
#ifdef CRC_MODULE_ENABLED
#include "at32f435_437_crc.h"
#endif
#ifdef WWDT_MODULE_ENABLED
#include "at32f435_437_wwdt.h"
#endif
#ifdef WDT_MODULE_ENABLED
#include "at32f435_437_wdt.h"
#endif
#ifdef EXINT_MODULE_ENABLED
#include "at32f435_437_exint.h"
#endif
#ifdef SDIO_MODULE_ENABLED
#include "at32f435_437_sdio.h"
#endif
#ifdef XMC_MODULE_ENABLED
#include "at32f435_437_xmc.h"
#endif
#ifdef ACC_MODULE_ENABLED
#include "at32f435_437_acc.h"
#endif
#ifdef MISC_MODULE_ENABLED
#include "at32f435_437_misc.h"
#endif
#ifdef EDMA_MODULE_ENABLED
#include "at32f435_437_edma.h"
#endif
#ifdef SCFG_MODULE_ENABLED
#include "at32f435_437_scfg.h"
#endif
#ifdef USB_MODULE_ENABLED
#include "at32f435_437_usb.h"
#endif
