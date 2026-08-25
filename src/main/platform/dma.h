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

// RP2350/RP2354 (PICO) compatibility shim.
//
// This file exists only so that betaflight-derived *_pico.c drivers (ported
// verbatim) can `#include "platform/dma.h"` unmodified. All of the actual
// dmaIdentifier_e / DEFINE_DMA_CHANNEL / DMA_FIRST_HANDLER content lives in
// wingflight's own shared src/main/drivers/dma.h (in its `#elif defined(PICO)`
// branch), matching wingflight's existing per-MCU-family pattern there rather
// than introducing a second, competing home for that content.

#pragma once

#include "drivers/dma.h"

#define DMA_INVALID (-1)

#define DMA_CH0_HANDLER  (DMA_FIRST_HANDLER)
#define DMA_CH1_HANDLER  (DMA_FIRST_HANDLER + 1)
#define DMA_CH2_HANDLER  (DMA_FIRST_HANDLER + 2)
#define DMA_CH3_HANDLER  (DMA_FIRST_HANDLER + 3)
#define DMA_CH4_HANDLER  (DMA_FIRST_HANDLER + 4)
#define DMA_CH5_HANDLER  (DMA_FIRST_HANDLER + 5)
#define DMA_CH6_HANDLER  (DMA_FIRST_HANDLER + 6)
#define DMA_CH7_HANDLER  (DMA_FIRST_HANDLER + 7)
#define DMA_CH8_HANDLER  (DMA_FIRST_HANDLER + 8)
#define DMA_CH9_HANDLER  (DMA_FIRST_HANDLER + 9)
#define DMA_CH10_HANDLER (DMA_FIRST_HANDLER + 10)
#define DMA_CH11_HANDLER (DMA_FIRST_HANDLER + 11)
#ifdef RP2350
#define DMA_CH12_HANDLER (DMA_FIRST_HANDLER + 12)
#define DMA_CH13_HANDLER (DMA_FIRST_HANDLER + 13)
#define DMA_CH14_HANDLER (DMA_FIRST_HANDLER + 14)
#define DMA_CH15_HANDLER (DMA_FIRST_HANDLER + 15)
#define DMA_LAST_HANDLER DMA_CH15_HANDLER
#else
#define DMA_LAST_HANDLER DMA_CH11_HANDLER
#endif

#define DMA_IDENTIFIER_TO_CHANNEL(identifier) ((identifier) - DMA_FIRST_HANDLER)
#define DMA_CHANNEL_TO_IDENTIFIER(channel) ((dmaIdentifier_e)((channel) + DMA_FIRST_HANDLER))
#define DMA_CHANNEL_TO_INDEX(channel) (channel)

dmaIdentifier_e dmaGetFreeIdentifier(void);
