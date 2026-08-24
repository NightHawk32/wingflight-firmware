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
 * AT32 override stub for ST's usbd_msc_core.h (lib/main/STM32_USB_Device_Library/Class/
 * msc/inc, not usable here). Quote-included by msc/usbd_storage.h's non-HAL branch,
 * purely for parity with the STM32F4 (non-HAL) build -- the one symbol ST's version
 * declares (USBD_Class_cb_TypeDef USBD_MSC_cb) is never referenced by
 * msc/usbd_storage.c or msc/usbd_storage_emfat.c, so no content is required here for
 * the AT32 (Artery usbd_core) build.
 */

#pragma once
