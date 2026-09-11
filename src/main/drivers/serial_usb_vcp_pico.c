/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Betaflight is distributed in the hope that it will be useful,
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

#include <stdint.h>
#include <stdbool.h>

#include "platform.h"

#ifdef USE_VCP

#include "build/build_config.h"

#include "common/utils.h"

#include "drivers/io.h"
#include "drivers/time.h"
#include "drivers/serial.h"
#include "drivers/serial_usb_vcp.h"

#include "platform/multicore.h"

#include "drivers/multicore_ringbuffer.h"
#include "drivers/usb_pico/usb_cdc.h"

static vcpPort_t vcpPort = { 0 };

// --- Transmit offload to core 1 -----------------------------------------
//
// cdc_usb_write() is not a queue-and-return call: it takes the TinyUSB mutex,
// runs tud_task() itself, and spins on a full endpoint FIFO for up to a
// millisecond. Called straight from taskHandleSerial(), that puts host-paced
// USB latency directly into the flight loop - the SERIAL task was measured
// peaking near 800us while a CLI session streamed.
//
// So core 0 only ever copies into the ring below and returns; core 1 drains it
// into the USB stack from its round-robin loop (see multicoreRegisterConsumer()
// in platform/multicore.h). This is the "work buffers" transport from
// docs/RP2350-Porting-Plan.md, and the VCP is its first producer.
//
// Without USE_MULTICORE - or if core 1 is not running - registration fails and
// every path below falls back to writing inline, exactly as before.
#ifdef USE_MULTICORE
#define VCP_TX_RING_SIZE 2048 // power of two, see multicore_ringbuffer.h

// How long core 0 keeps offering a chunk while the ring refuses to take any
// more of it. The clock restarts on every byte accepted, so this is "the host
// has stopped reading entirely", not "the host is slower than the CLI" - a
// bulk dump that outruns the host stalls here exactly as it did when the write
// went straight to cdc_usb_write(), instead of losing a block of output. Only
// a host that has genuinely gone away hits the limit, and then the data is
// dropped rather than the loop stalled indefinitely.
#define VCP_TX_STALL_TIMEOUT_US 250000

static uint8_t vcpTxRingBuffer[VCP_TX_RING_SIZE];
static multicoreRingBuffer_t vcpTxRing = MULTICORE_RINGBUFFER_INIT(vcpTxRingBuffer, VCP_TX_RING_SIZE);
static bool vcpTxOffloaded;

// Drains one bounded chunk per call - the contract multicoreRegisterConsumer()
// requires, so the VCP cannot starve core 1's other consumers. Only ever hands
// the stack as much as the endpoint can take right now, so cdc_usb_write()
// returns without spinning and the TinyUSB mutex is held only briefly (core 0
// still takes it to read).
static void vcpTxDrain(void *ctx)
{
    UNUSED(ctx);

    // Core 1 owns the device stack now, so its loop is what keeps the stack
    // running: enumeration, control transfers and endpoint completions all
    // come out of here as well as out of the USB interrupt.
    cdc_usb_background_task();

    if (!cdc_usb_connected() || !cdc_usb_configured()) {
        // Nobody is listening. Discard rather than accumulate, so a CLI
        // session that outlives its host does not wedge the ring full.
        const uint8_t *run;
        uint16_t n;
        while ((n = multicoreRingBufferPeekRun(&vcpTxRing, &run)) != 0) {
            multicoreRingBufferConsume(&vcpTxRing, n);
        }
        return;
    }

    const uint32_t canTake = cdc_usb_tx_bytes_free();
    if (!canTake) {
        return;
    }

    const uint8_t *run;
    uint16_t n = multicoreRingBufferPeekRun(&vcpTxRing, &run);
    if (!n) {
        return;
    }
    if (n > canTake) {
        n = (uint16_t)canTake;
    }

    const int written = cdc_usb_write(run, n);
    if (written > 0) {
        multicoreRingBufferConsume(&vcpTxRing, (uint16_t)written);
    }
}
#endif // USE_MULTICORE

// Writes through the offload ring when core 1 owns the USB stack, otherwise
// inline. Returns false if anything had to be dropped.
static bool usbVcpSend(const uint8_t *data, int count)
{
#ifdef USE_MULTICORE
    if (vcpTxOffloaded) {
        timeUs_t lastProgressUs = microsISR();
        while (count > 0) {
            const uint16_t taken = multicoreRingBufferPushBuf(&vcpTxRing, data, (uint16_t)MIN(count, UINT16_MAX));
            data += taken;
            count -= taken;

            if (taken) {
                lastProgressUs = microsISR();
            } else if (cmpTimeUs(microsISR(), lastProgressUs) > VCP_TX_STALL_TIMEOUT_US) {
                return false; // nothing is draining the ring; drop the rest
            }
        }
        return true;
    }
#endif

    if (!(cdc_usb_connected() && cdc_usb_configured())) {
        return false;
    }

    while (count > 0) {
        const int txed = cdc_usb_write(data, count);
        if (txed <= 0) {
            return false;
        }
        count -= txed;
        data += txed;
    }
    return true;
}

static void usbVcpSetBaudRate(serialPort_t *instance, uint32_t baudRate)
{
    UNUSED(instance);
    UNUSED(baudRate);
}

static void usbVcpSetMode(serialPort_t *instance, portMode_e mode)
{
    UNUSED(instance);
    UNUSED(mode);
}

static void usbVcpSetCtrlLineStateCb(serialPort_t *instance, void (*cb)(void *context, uint16_t ctrlLineState), void *context)
{
    UNUSED(instance);
    UNUSED(cb);
    UNUSED(context);
}

static void usbVcpSetBaudRateCb(serialPort_t *instance, void (*cb)(serialPort_t *context, uint32_t baud), serialPort_t *context)
{
    UNUSED(instance);
    UNUSED(cb);
    UNUSED(context);
}

static bool isUsbVcpTransmitBufferEmpty(const serialPort_t *instance)
{
    UNUSED(instance);
#ifdef USE_MULTICORE
    if (vcpTxOffloaded) {
        return multicoreRingBufferBytesUsed(&vcpTxRing) == 0;
    }
#endif
    return true;
}

static uint32_t usbVcpRxBytesAvailable(const serialPort_t *instance)
{
    UNUSED(instance);
    return cdc_usb_bytes_available();
}

static uint8_t usbVcpRead(serialPort_t *instance)
{
    UNUSED(instance);

    uint8_t buf[1];

    while (true) {
        if (cdc_usb_read(buf, 1)) {
            return buf[0];
        }
    }
}

static void usbVcpWriteBuf(serialPort_t *instance, const void *data, int count)
{
    UNUSED(instance);

    usbVcpSend((const uint8_t *)data, count);
}

static bool usbVcpFlush(vcpPort_t *port)
{
    uint32_t count = port->txAt;
    port->txAt = 0;

    if (count == 0) {
        return true;
    }

    return usbVcpSend(port->txBuf, count);
}

static void usbVcpWrite(serialPort_t *instance, uint8_t c)
{
    vcpPort_t *port = container_of(instance, vcpPort_t, port);

    port->txBuf[port->txAt++] = c;
    if (!port->buffering || port->txAt >= ARRAYLEN(port->txBuf)) {
        usbVcpFlush(port);
    }
}

static void usbVcpBeginWrite(serialPort_t *instance)
{
    vcpPort_t *port = container_of(instance, vcpPort_t, port);
    port->buffering = true;
}

static uint32_t usbTxBytesFree(const serialPort_t *instance)
{
    UNUSED(instance);
#ifdef USE_MULTICORE
    if (vcpTxOffloaded) {
        return multicoreRingBufferBytesFree(&vcpTxRing);
    }
#endif
    return cdc_usb_tx_bytes_free();
}

static void usbVcpEndWrite(serialPort_t *instance)
{
    vcpPort_t *port = container_of(instance, vcpPort_t, port);
    port->buffering = false;
    usbVcpFlush(port);
}

static const struct serialPortVTable usbVTable[] = {
    {
        .serialWrite = usbVcpWrite,
        .serialTotalRxWaiting = usbVcpRxBytesAvailable,
        .serialTotalTxFree = usbTxBytesFree,
        .serialRead = usbVcpRead,
        .serialSetBaudRate = usbVcpSetBaudRate,
        .isSerialTransmitBufferEmpty = isUsbVcpTransmitBufferEmpty,
        .setMode = usbVcpSetMode,
        .setCtrlLineStateCb = usbVcpSetCtrlLineStateCb,
        .setBaudRateCb = usbVcpSetBaudRateCb,
        .writeBuf = usbVcpWriteBuf,
        .beginWrite = usbVcpBeginWrite,
        .endWrite = usbVcpEndWrite
    }
};

void usbVcpInit(void)
{
    // initialise the USB CDC interface using core 0
    cdc_usb_init();

#ifdef USE_MULTICORE
    // Hand transmission - and with it the stack itself - to core 1, but only
    // if core 1 will actually pump it. multicoreRegisterConsumer() reports
    // false on a single-core build or a full consumer table, and if the move
    // is refused after that, ownership stays on core 0 and so must the writes:
    // the two decisions cannot disagree, or every write would go to a stack
    // nothing is servicing.
    vcpTxOffloaded = multicoreRegisterConsumer(vcpTxDrain, NULL) && cdc_usb_move_to_core1();
#endif
}

serialPort_t *usbVcpOpen(void)
{
    vcpPort_t *s = &vcpPort;
    s->port.vTable = usbVTable;
    return &s->port;
}

uint32_t usbVcpGetBaudRate(serialPort_t *instance)
{
    UNUSED(instance);
    return cdc_usb_baud_rate();
}

uint8_t usbVcpIsConnected(void)
{
    return cdc_usb_connected();
}

uint8_t usbVcpIsActive(void)
{
    // tud_cdc_connected() already requires mounted and not suspended, so it never latches
    return cdc_usb_connected();
}

#endif // USE_VCP
