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

/*
 * ESCape32 forward programming link
 *
 * ESCape32 enters its CLI after the signal line has been held high for ~1s
 * (the 4-way interface does this), then talks half-duplex 38400 8N1 on the
 * signal wire. Its binary config commands avoid carrying parameter names:
 *
 *   Request:  0x01 <len> <cmd> [<data>...] <crc>
 *   Response: 0x01 <len> <cmd> <res> <payload> <crc>
 *
 * <len> is the whole frame, <crc> is CRC-8 poly 0x07 over the preceding bytes,
 * <res> is 0 on success. <payload> is the config (see serial_4way_escape32.h)
 * and is passed through MSP_ESC_PARAMETERS unchanged; value i is ESCape32
 * parameter i (CFG_MAP order in ESCape32 src/prog.c).
 *
 *   0x01 Get: no data
 *   0x02 Set and save: <rev> <cnt> <cnt int16 LE values>
 *
 * The 26us bit time is too tight for micros() polling with interrupts
 * enabled, so frames are clocked from the DWT cycle counter with
 * interrupts masked. This only runs disarmed, with motors disabled.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#if defined(USE_SERIAL_4WAY_BLHELI_INTERFACE) && defined(USE_ESCAPE32_FORWARD_PROGRAMMING)

#include "common/crc.h"

#include "drivers/io.h"
#include "drivers/system.h"

#include "io/serial_4way.h"
#include "io/serial_4way_impl.h"
#include "io/serial_4way_escape32.h"

#define ESC32_SOH               0x01
#define ESC32_CMD_GET           0x01
#define ESC32_CMD_SET           0x02
#define ESC32_CRC_POLY          0x07
#define ESC32_FRAME_OVERHEAD    5           // SOH, len, cmd, res, crc
#define ESC32_FRAME_MAX_SIZE    (ESC32_PAYLOAD_MAX_SIZE + ESC32_FRAME_OVERHEAD)

#define ESC32_BIT_US_X24        625         // 38400 baud bit time is 625/24 us
#define ESC32_GET_TIMEOUT_US    20000
#define ESC32_SET_TIMEOUT_US    500000      // ESC erases and writes a flash page
#define ESC32_BYTE_TIMEOUT_BITS 20
#define ESC32_POLL_SLICE_US     1000
#define ESC32_RETRY_DELAY_US    100000
#define ESC32_GET_ATTEMPTS      2
#define ESC32_SET_ATTEMPTS      2

static uint8_t frame[ESC32_FRAME_MAX_SIZE];
static uint32_t bitCycles;

static bool elapsed(uint32_t start, uint32_t cycles)
{
    return getCycleCounter() - start >= cycles;
}

static void waitUntil(uint32_t t)
{
    while ((int32_t)(getCycleCounter() - t) < 0);
}

// Interrupts must be masked
static void sendFrame(uint8_t len)
{
    ESC_OUTPUT;
    uint32_t t = getCycleCounter();
    for (int i = 0; i < len; i++) {
        uint16_t bits = (frame[i] << 1) | 0x200;   // start, 8 data LSB first, stop
        for (int n = 0; n < 10; n++) {
            if (bits & 1) {
                ESC_SET_HI;
            } else {
                ESC_SET_LO;
            }
            bits >>= 1;
            t += bitCycles;
            waitUntil(t);
        }
    }
    ESC_INPUT;
}

// Interrupts must be masked. 't' is the middle of data bit 0.
static bool recvBits(uint32_t t, uint8_t *byte)
{
    uint8_t b = 0;

    for (int n = 0; n < 8; n++) {
        waitUntil(t);
        if (ESC_IS_HI) {
            b |= 1 << n;
        }
        t += bitCycles;
    }
    waitUntil(t);
    *byte = b;
    return ESC_IS_HI;   // stop bit
}

// Interrupts must be masked. Returns frame length, 0 on error.
static uint8_t recvFrameBody(void)
{
    uint32_t t = getCycleCounter();

    // Every frame starts with SOH: its data bit 0 is the first rising edge,
    // which locates the bit clock even if the start edge was seen late.
    while (ESC_IS_LO) {
        if (elapsed(t, 2 * bitCycles)) {
            return 0;
        }
    }
    t = getCycleCounter() + bitCycles / 2;
    if (!recvBits(t, &frame[0]) || frame[0] != ESC32_SOH) {
        return 0;
    }

    uint8_t len = 2;
    for (int i = 1; i < len; i++) {
        t = getCycleCounter();
        while (ESC_IS_HI) {
            if (elapsed(t, ESC32_BYTE_TIMEOUT_BITS * bitCycles)) {
                return 0;
            }
        }
        t = getCycleCounter() + bitCycles + bitCycles / 2;
        if (!recvBits(t, &frame[i])) {
            return 0;
        }
        if (i == 1) {
            len = frame[1];
            if (len < ESC32_FRAME_OVERHEAD || len > ESC32_FRAME_MAX_SIZE) {
                return 0;
            }
        }
    }

    return len;
}

// Request is in 'frame' with SOH, len and cmd set. Returns response length.
static uint8_t transfer(uint8_t escIdx, uint32_t timeoutUs)
{
    const uint8_t cmd = frame[2];
    const uint8_t reqLen = frame[1];
    uint8_t len = 0;

    selected_esc = escIdx;
    bitCycles = clockMicrosToCycles(ESC32_BIT_US_X24) / 24;
    frame[reqLen - 1] = crc8_update(0, frame, reqLen - 1, ESC32_CRC_POLY);

    // The reply is received with interrupts masked. While the ESC is busy
    // (e.g. saving) they are let through between polling slices.
    const uint32_t start = getCycleCounter();
    const uint32_t timeout = clockMicrosToCycles(timeoutUs);
    const uint32_t slice = clockMicrosToCycles(ESC32_POLL_SLICE_US);
    bool started = false;

    __disable_irq();
    sendFrame(reqLen);
    while (true) {
        const uint32_t t = getCycleCounter();
        while (!elapsed(t, slice)) {
            if (ESC_IS_LO) {
                started = true;
                len = recvFrameBody();
                break;
            }
        }
        __enable_irq();
        if (started || elapsed(start, timeout)) {
            break;
        }
        __disable_irq();
    }

    if (len < ESC32_FRAME_OVERHEAD ||
        frame[2] != cmd ||
        crc8_update(0, frame, len, ESC32_CRC_POLY) != 0 ||
        frame[3] != 0 ||
        len - ESC32_FRAME_OVERHEAD < ESC32_PAYLOAD_HEADER_SIZE) {
        return 0;
    }

    return len;
}

static uint8_t copyPayload(uint8_t len, uint8_t *payload)
{
    len -= ESC32_FRAME_OVERHEAD;
    memcpy(payload, frame + 4, len);
    return len;
}

uint8_t esc32ReadConfig(uint8_t escIdx, uint8_t *payload)
{
    for (int attempt = 0; attempt < ESC32_GET_ATTEMPTS; attempt++) {
        if (attempt) {
            // Stay idle for longer than the ESC's CLI entry timer, so an ESC
            // that is still booting is not held off by our edges
            const uint32_t t = getCycleCounter();
            while (!elapsed(t, clockMicrosToCycles(ESC32_RETRY_DELAY_US)));
        }
        frame[0] = ESC32_SOH;
        frame[1] = 4;
        frame[2] = ESC32_CMD_GET;
        const uint8_t len = transfer(escIdx, ESC32_GET_TIMEOUT_US);
        if (len) {
            return copyPayload(len, payload);
        }
    }

    return 0;
}

uint8_t esc32WriteConfig(uint8_t escIdx, const uint8_t *payload, uint8_t len, uint8_t *result)
{
    if (len < ESC32_PAYLOAD_HEADER_SIZE || len > ESC32_PAYLOAD_MAX_SIZE) {
        return 0;
    }

    const uint8_t cnt = payload[ESC32_PAYLOAD_HEADER_SIZE - 1];
    const uint8_t valLen = len - ESC32_PAYLOAD_HEADER_SIZE;
    if (valLen != cnt * 2) {
        return 0;
    }

    for (int attempt = 0; attempt < ESC32_SET_ATTEMPTS; attempt++) {
        frame[0] = ESC32_SOH;
        frame[1] = 6 + valLen;
        frame[2] = ESC32_CMD_SET;
        frame[3] = payload[0];      // revision
        frame[4] = cnt;
        memcpy(frame + 5, payload + ESC32_PAYLOAD_HEADER_SIZE, valLen);
        const uint8_t resLen = transfer(escIdx, ESC32_SET_TIMEOUT_US);
        if (resLen) {
            return copyPayload(resLen, result);
        }
    }

    return 0;
}

#endif
