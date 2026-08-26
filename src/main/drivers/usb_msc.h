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
 * Author: Chris Hockuba (https://github.com/conkerkh)
 */

#pragma once

void mscInit(void);
bool mscCheckBootAndReset(void);
uint8_t mscStart(void);
// Service the USB device stack while in MSC mode. Only needed (and only
// defined) on platforms whose USB stack is polled rather than
// interrupt-driven - PICO's TinyUSB (usb_pico/usb_msc_pico.c's tud_task()
// pump). STM32's USB-OTG MSC path is fully interrupt-driven and defines no
// mscTask(); mscWaitForButton() only calls this under #if defined(PICO).
void mscTask(void);
bool mscCheckButton(void);
void mscWaitForButton(void);
void systemResetToMsc(int timezoneOffsetMinutes);
void systemResetFromMsc(void);
void mscSetActive(void);
void mscActivityLed(void);
