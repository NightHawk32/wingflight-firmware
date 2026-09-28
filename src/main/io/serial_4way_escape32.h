/*
 * This file is part of Wingflight.
 *
 * Wingflight is free software. You can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wingflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>

// Config payload: rev, patch, name[16], cnt, then cnt int16 LE values
#define ESC32_PAYLOAD_HEADER_SIZE   19
#define ESC32_PAYLOAD_MAX_SIZE      126

// Both return the ESC's current config payload length, 0 on failure.
// Requires the 4-way interface to be active (motor lines held high).
uint8_t esc32ReadConfig(uint8_t escIdx, uint8_t *payload);
uint8_t esc32WriteConfig(uint8_t escIdx, const uint8_t *payload, uint8_t len, uint8_t *result);
