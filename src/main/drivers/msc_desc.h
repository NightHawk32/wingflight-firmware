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
 * Override of Artery's default lib/main/AT32F43x/middlewares/usbd_class/msc/msc_desc.h.
 *
 * The vendored msc_class.c/msc_desc.c include this via #include <msc_desc.h> (angle
 * brackets), so this file must appear in a -I directory listed BEFORE
 * $(MIDDLEWARES_DIR)/usbd_class/msc in make/mcu/AT32F4.mk's INCLUDE_DIRS for this
 * override to take effect (it does -- see the ordering there). Only the branding/ID
 * defines below differ from the vendor default; everything else must stay in sync with
 * lib/main/AT32F43x/middlewares/usbd_class/msc/msc_desc.h.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "msc_class.h"
#include "usbd_core.h"

#define MSC_BCD_NUM                          0x0110

#define USBD_MSC_VENDOR_ID                   0x2E3C
#define USBD_MSC_PRODUCT_ID                  0x5720

#define USBD_MSC_CONFIG_DESC_SIZE            32
#define USBD_MSC_SIZ_STRING_LANGID           4
#define USBD_MSC_SIZ_STRING_SERIAL           0x1A

#define USBD_MSC_DESC_MANUFACTURER_STRING    "Artery"
#define USBD_MSC_DESC_PRODUCT_STRING         "Wingflight FC Mass Storage"
#define USBD_MSC_DESC_CONFIGURATION_STRING   "MSC Config"
#define USBD_MSC_DESC_INTERFACE_STRING       "MSC Interface"

#define         MCU_ID1                   (0x1FFFF7E8)
#define         MCU_ID2                   (0x1FFFF7EC)
#define         MCU_ID3                   (0x1FFFF7F0)

extern usbd_desc_handler msc_desc_handler;

#ifdef __cplusplus
}
#endif
