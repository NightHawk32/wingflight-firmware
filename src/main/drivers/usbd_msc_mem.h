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
 * AT32 override of ST's usbd_msc_mem.h (lib/main/STM32_USB_Device_Library/Class/msc/inc,
 * not usable here since AT32 doesn't use that library). Quote-included by
 * msc/usbd_storage.h's non-HAL branch; provides the USBD_STORAGE_cb_TypeDef contract
 * that drivers/msc_diskio.h's callbacks (in usb_msc_at32f43x.c) are wrapped by.
 * Content matches betaflight's own AT32 platform reference file verbatim.
 */

#pragma once

#define USBD_STD_INQUIRY_LENGTH     36

typedef struct _USBD_STORAGE
{
  int8_t (* Init) (uint8_t lun);
  int8_t (* GetCapacity) (uint8_t lun, uint32_t *block_num, uint32_t *block_size);
  int8_t (* IsReady) (uint8_t lun);
  int8_t (* IsWriteProtected) (uint8_t lun);
  int8_t (* Read) (uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
  int8_t (* Write)(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
  int8_t (* GetMaxLun)(void);
  int8_t *pInquiry;

} USBD_STORAGE_cb_TypeDef;

extern USBD_STORAGE_cb_TypeDef *USBD_STORAGE_fops;
