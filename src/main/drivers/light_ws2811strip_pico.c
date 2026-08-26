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

/*
 * PICO WS2811/WS2812/SK6812 LED strip backend for Wingflight's shared
 * light_ws2811strip.c core.
 *
 * The shared core (updateLEDDMABuffer()) fills ledStripDMABuffer[] with ONE
 * uint32_t word PER LED BIT, each word holding BIT_COMPARE_1 or
 * BIT_COMPARE_0 - the format STM32's timer-compare DMA expects. Rather than
 * rewriting that core loop for PICO (per-LED packed words would break the
 * per-LED format-inversion feature, which can mix 24-bit GRB and 32-bit
 * GRBW LEDs in one strip against a fixed PIO autopull threshold), this
 * driver keeps the one-word-per-bit stream and consumes it as-is: the state
 * machine shifts RIGHT with an autopull threshold of 1, so each DMA'd word
 * yields exactly its least-significant bit. With BIT_COMPARE_1 = 1 and
 * BIT_COMPARE_0 = 0 the shared buffer is directly a valid PIO bitstream.
 * DMA bandwidth cost (32x the strict minimum, ~4KB/frame at 32 LEDs) is
 * negligible against the 1.25us/bit wire rate that paces the transfer.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_LED_STRIP

#include "common/color.h"
#include "common/maths.h"

#include "drivers/dma.h"
#include "platform/dma.h"
#include "drivers/io.h"
#include "drivers/time.h"
#include "drivers/light_ws2811strip.h"
#include "drivers/nvic.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"

#define WS2812_WRAP_TARGET 0
#define WS2812_WRAP 3
#define WS2812_PIO_VERSION 0

// 10 PIO cycles per bit at 800kHz: T0H = 3 cycles (375ns), T1H = 6 cycles
// (750ns), both within WS2812/SK6812 tolerance.
#define WS2812_T1 3
#define WS2812_T2 3
#define WS2812_T3 4

static IO_t ledStripIO = IO_NONE;

// DMA channel
static uint8_t dma_chan;

static timeUs_t ledStripCompletedTime = 0;

static const uint16_t ws2812_program_instructions[] = {
            //     .wrap_target
    0x6321, //  0: out    x, 1            side 0 [3]
    0x1223, //  1: jmp    !x, 3           side 1 [2]
    0x1200, //  2: jmp    0               side 1 [2]
    0xa242, //  3: nop                    side 0 [2]
            //     .wrap
};

static const struct pio_program ws2812_program = {
    .instructions = ws2812_program_instructions,
    .length = ARRAYLEN(ws2812_program_instructions),
    .origin = -1,
    .pio_version = WS2812_PIO_VERSION,
    .used_gpio_ranges = 0x0
};

static inline pio_sm_config ws2812_program_get_default_config(uint offset)
{
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + WS2812_WRAP_TARGET, offset + WS2812_WRAP);
    sm_config_set_sideset(&c, 1, false, false);
    return c;
}

static bool ws2812_program_init(PIO pio, uint sm, uint offset, uint pin)
{
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, pin);
    // Shift right, autopull with a threshold of 1: one wire bit per 32-bit
    // FIFO word, taken from the word's LSB (the BIT_COMPARE_x value).
    sm_config_set_out_shift(&c, true, true, 1);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    int cycles_per_bit = WS2812_T1 + WS2812_T2 + WS2812_T3;
    float div = clock_get_hz(clk_sys) / ((float)WS2811_CARRIER_HZ * cycles_per_bit);
    sm_config_set_clkdiv(&c, div);
    // Rejected (PICO_ERROR_BAD_ALIGNMENT) if the pin is outside the block's
    // GPIO-base window - never enable an SM whose config wasn't written.
    if (pio_sm_init(pio, sm, offset, &c) != PICO_OK) {
        return false;
    }
    pio_sm_set_enabled(pio, sm, true);
    return true;
}

static FAST_IRQ_HANDLER void ws2811LedStripDmaHandler(dmaChannelDescriptor_t* descriptor)
{
    UNUSED(descriptor);
    ws2811LedDataTransferInProgress = false;
    ledStripCompletedTime = micros();
}

bool ws2811LedStripHardwareInit(ioTag_t ioTag)
{
    if (!ioTag) {
        return false;
    }

    IO_t io = IOGetByTag(ioTag);
    if (!IOIsFreeOrPreinit(io)) {
        return false;
    }

    // Values consumed by the shared core's updateLEDDMABuffer(): each buffer
    // word's LSB is the wire bit (see file header). The globals are owned by
    // light_ws2811strip.c (zero-initialised there); set them the way the
    // STM32 backends derive theirs from the timer period.
    BIT_COMPARE_1 = 1;
    BIT_COMPARE_0 = 0;

    const PIO pio = PIO_INSTANCE(PIO_LEDSTRIP_INDEX);

    int pinIndex = DEFIO_TAG_PIN(ioTag);
    if (pinIndex >= 32) {
        pio_set_gpio_base(pio, 16);
    }
    if (!pio_can_add_program(pio, &ws2812_program)) {
        return false;
    }
    int offset = pio_add_program(pio, &ws2812_program);
    int pio_sm = pio_claim_unused_sm(pio, false);
    if (pio_sm < 0) {
        return false;
    }

    if (!ws2812_program_init(pio, pio_sm, offset, pinIndex)) {
        pio_sm_unclaim(pio, pio_sm);
        return false;
    }

    // --- DMA Configuration ---
    const dmaIdentifier_e dma_id = dmaGetFreeIdentifier();
    if (dma_id == DMA_NONE) {
        return false;
    }
    if (!dmaAllocate(dma_id, OWNER_LED_STRIP, 0)) {
        // dmaGetFreeIdentifier() already claimed the channel in the SDK -
        // release it or it leaks for good.
        dma_channel_unclaim(DMA_IDENTIFIER_TO_CHANNEL(dma_id));
        return false;
    }
    dma_chan = DMA_IDENTIFIER_TO_CHANNEL(dma_id);

    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, pio_sm, true));

    dma_channel_configure(
        dma_chan,
        &c,
        &pio->txf[pio_sm],             // Write address (PIO TX FIFO)
        NULL,                          // Read address (set per transfer)
        WS2811_DMA_BUFFER_SIZE,        // One word per LED bit (see header)
        false                          // Don't start immediately
    );

    // --- Interrupt Configuration ---
    dmaSetHandler(dma_id, ws2811LedStripDmaHandler, NVIC_PRIO_WS2811_DMA, 0);

    IOInit(io, OWNER_LED_STRIP, 0);
    ledStripIO = io;
    return true;
}

void ws2811LedStripDMAEnable(void)
{
    if (!ledStripIO) {
        ws2811LedDataTransferInProgress = false;
        return; // Not initialized
    }

    // Honour the WS281x >=50us reset/latch period. The DMA-complete IRQ
    // fires while the joined TX FIFO still holds up to 8 words plus one in
    // the OSR (~11us of wire time at 1.25us/bit), so measure from the IRQ
    // with margin, and skip (not block) if a new frame comes implausibly
    // fast. Note the reset itself comes from the SM stalling low on
    // `out x,1 side 0` once the FIFO drains - not from trailing buffer words.
    if (ledStripCompletedTime != 0 && ABS(cmpTimeUs(micros(), ledStripCompletedTime)) < 50 + 11 + 10) {
        ws2811LedDataTransferInProgress = false;
        return;
    }

    // Unlike STM32 (where the buffer's unused tail holds timer-compare 0 =
    // line held low), on PIO every trailing BIT_COMPARE_0 word is a genuine
    // WS281x "0" bit shifted past the end of the physical strip: with the
    // shared core filling only ledCount*bitsPerLed words, the remaining
    // ~300 words at 24-bit GRB cost ~370us of extra wire time per frame.
    // Harmless (LEDs past the end don't exist) and kept for the same
    // "always send the full buffer" contract as the STM32 backends, since
    // the per-LED bit width isn't known here.
    dma_channel_set_read_addr(dma_chan, ledStripDMABuffer, false);
    dma_channel_set_trans_count(dma_chan, WS2811_DMA_BUFFER_SIZE, false);
    // Start the DMA transfer
    dma_channel_start(dma_chan);
}

#endif // USE_LED_STRIP
