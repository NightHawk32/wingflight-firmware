/*
 * This file is part of Wingflight.
 *
 * Wingflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Wingflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Artery AT32F435/437 USB OTG device-mode configuration.
 *
 * Placed in src/main/drivers/ (rather than a subdirectory) so it is found
 * both via plain #include "usb_conf.h" from files that live alongside it
 * (quote-form search checks the including file's own directory first) and,
 * for vendored middleware files that live elsewhere (usb_core.h/usbd_core.h/
 * usbd_sdr.h/usb_std.h all do #include "usb_conf.h"), via the explicit
 * $(ROOT)/src/main/drivers entry added to INCLUDE_DIRS in make/mcu/AT32F4.mk.
 *
 * Pin assignments below are fixed AT32F435/437 OTGFS1 silicon routing (not
 * board-configurable pin-mux choices), confirmed against Artery's own
 * usb_device_vcp_loopback example -- adapted here from
 * betaflight/src/platform/AT32/usb_conf.h.
 */

#pragma once

#include "at32f435_437_usb.h"
#include "at32f435_437.h"

#define USE_OTG_DEVICE_MODE

/* use otgfs1 */
#define OTG_USB_ID                           1

#if (OTG_USB_ID == 1)
#define USB_ID                           0
#define OTG_CLOCK                        CRM_OTGFS1_PERIPH_CLOCK
#define OTG_IRQ                          OTGFS1_IRQn
#define OTG_IRQ_HANDLER                  OTGFS1_IRQHandler
#define OTG_WKUP_IRQ                     OTGFS1_WKUP_IRQn
#define OTG_WKUP_HANDLER                 OTGFS1_WKUP_IRQHandler
#define OTG_WKUP_EXINT_LINE              EXINT_LINE_18

#define OTG_PIN_GPIO                     GPIOA
#define OTG_PIN_GPIO_CLOCK               CRM_GPIOA_PERIPH_CLOCK

#define OTG_PIN_DP                       GPIO_PINS_12
#define OTG_PIN_DP_SOURCE                GPIO_PINS_SOURCE12

#define OTG_PIN_DM                       GPIO_PINS_11
#define OTG_PIN_DM_SOURCE                GPIO_PINS_SOURCE11

#define OTG_PIN_VBUS                     GPIO_PINS_9
#define OTG_PIN_VBUS_SOURCE              GPIO_PINS_SOURCE9

#define OTG_PIN_ID                       GPIO_PINS_10
#define OTG_PIN_ID_SOURCE                GPIO_PINS_SOURCE10

#define OTG_PIN_SOF_GPIO                 GPIOA
#define OTG_PIN_SOF_GPIO_CLOCK           CRM_GPIOA_PERIPH_CLOCK
#define OTG_PIN_SOF                      GPIO_PINS_8
#define OTG_PIN_SOF_SOURCE               GPIO_PINS_SOURCE8

#define OTG_PIN_MUX                      GPIO_MUX_10
#endif

#ifdef USE_OTG_DEVICE_MODE
/* otg1 device fifo */
#define USBD_RX_SIZE                     128
#define USBD_EP0_TX_SIZE                 24
#define USBD_EP1_TX_SIZE                 20
#define USBD_EP2_TX_SIZE                 20
#define USBD_EP3_TX_SIZE                 20
#define USBD_EP4_TX_SIZE                 20
#define USBD_EP5_TX_SIZE                 20
#define USBD_EP6_TX_SIZE                 20
#define USBD_EP7_TX_SIZE                 20

/* otg2 device fifo -- unused (OTG_USB_ID selects otgfs1 above), but usbd_core.c
 * references these unconditionally regardless of which core is active at runtime. */
#define USBD2_RX_SIZE                    128
#define USBD2_EP0_TX_SIZE                24
#define USBD2_EP1_TX_SIZE                20
#define USBD2_EP2_TX_SIZE                20
#define USBD2_EP3_TX_SIZE                20
#define USBD2_EP4_TX_SIZE                20
#define USBD2_EP5_TX_SIZE                20
#define USBD2_EP6_TX_SIZE                20
#define USBD2_EP7_TX_SIZE                20

#ifndef USB_EPT_MAX_NUM
#define USB_EPT_MAX_NUM                   8
#endif
#endif

/* usb sof output disabled */
/* #define USB_SOF_OUTPUT_ENABLE */

/* no VBUS sense pin wired on typical FC boards -- ignore VBUS */
#define USB_VBUS_IGNORE

/* remote-wakeup handling not used */
/* #define USB_LOW_POWER_WAKUP */

void usb_delay_ms(uint32_t ms);
void usb_delay_us(uint32_t us);
