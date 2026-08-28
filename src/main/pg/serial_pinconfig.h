/*
 * This file is part of Rotorflight.
 *
 * Rotorflight is free software. You can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Rotorflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "types.h"
#include "platform.h"

#include "config/config.h"

#include "drivers/io.h"

#include "pg/pg.h"

#if defined(USE_SOFTSERIAL1) || defined(USE_SOFTSERIAL2)
# ifdef USE_SOFTSERIAL2
#  define SERIAL_PORT_MAX_INDEX (RESOURCE_SOFT_OFFSET + 2)
# else
#  define SERIAL_PORT_MAX_INDEX (RESOURCE_SOFT_OFFSET + 1)
# endif
#else
# define SERIAL_PORT_MAX_INDEX RESOURCE_SOFT_OFFSET
#endif

#ifdef USE_SERIAL_BIDIR_SWITCH
// Only the hardware UARTs can need the external SPST combiner switch (soft
// serial's PIO half duplex shares one pin between two state machines, no
// switch involved), so the enable-pin array covers hardware-UART indices
// only - not the RESOURCE_SOFT_OFFSET-based soft-serial slots.
#define SERIAL_BIDIR_SWITCH_COUNT 2
#endif

typedef struct serialPinConfig_s {
    ioTag_t ioTagTx[SERIAL_PORT_MAX_INDEX];
    ioTag_t ioTagRx[SERIAL_PORT_MAX_INDEX];
    ioTag_t ioTagInverter[SERIAL_PORT_MAX_INDEX];
#ifdef USE_SERIAL_BIDIR_SWITCH
    // Enable pin for an external SPST switch joining a hardware UART's fixed
    // TX/RX pins for single-wire half duplex (SERIAL_BIDIR) - see
    // drivers/serial_uart_pico.c. Unused/NONE unless such a switch is fitted.
    ioTag_t ioTagBidirEnable[SERIAL_BIDIR_SWITCH_COUNT];
#endif
} serialPinConfig_t;

PG_DECLARE(serialPinConfig_t, serialPinConfig);
