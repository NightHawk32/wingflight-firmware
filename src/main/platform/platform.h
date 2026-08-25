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
// betaflight-derived *_pico.c drivers (ported verbatim) `#include
// "platform/platform.h"` to pull in PICO-specific types/macros
// (I2C_INST()/SPI_INST()/UART_INST(), opaque STM32-shaped placeholder types,
// etc). In wingflight all of that content already lives centrally in
// src/main/common/platform.h's `#elif defined(PICO)` branch (reached via the
// unconditional `#include "platform.h"` every driver file already has), so
// this file just re-includes it for drop-in compatibility with the ported
// file's include directive.

#pragma once

#include "platform.h"
