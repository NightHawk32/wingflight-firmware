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
 * PICO "soft serial": extra UARTs bit-banged by PIO state machines.
 *
 * Fills the SOFTSERIAL1/SOFTSERIAL2 slots of Wingflight's existing serial
 * architecture (identifiers, `resource SERIAL_TX/RX 11|12 <pin>` CLI
 * mapping via serialPinConfig()->ioTagTx/Rx[RESOURCE_SOFT_OFFSET + n], MSP
 * port reporting) instead of introducing a new port class - a PIO UART *is*
 * this platform's soft serial, just clocked by hardware rather than
 * timer-interrupt bit-banging, so unlike STM32 soft serial it is not
 * restricted to low baud rates.
 *
 * One PIO block (PIO_UART_INDEX, pio1 by default - see
 * RP2350_UNIFIED/target.h) hosts up to 2 ports x (1 TX + 1 RX) = 4 state
 * machines, sharing one TX program and one RX program (both from
 * pico-examples' uart_tx.pio/uart_rx.pio, 8 cycles per bit). 8N1 only.
 *
 * Interrupt model mirrors a hardware UART driver: the PIO block's IRQ0
 * fires on RX-FIFO-not-empty (always, per active RX SM) and on
 * TX-FIFO-not-full (enabled only while the TX ring buffer holds data,
 * exactly the classic TXE-interrupt pattern), so serial-RX providers that
 * rely on the per-byte rxCallback being invoked from interrupt context
 * (SBUS & friends) work the same as on a hardware UART.
 *
 * SERIAL_INVERTED is supported for free via the RP2350 pad-level
 * input/output override inverters (gpio_set_inover()/gpio_set_outover()).
 *
 * SERIAL_BIDIR (single-wire half-duplex, e.g. SmartAudio) reuses the TX pin
 * only (RX pin config is ignored, matching the STM32 half-duplex
 * convention in serial_softserial.c) and hands the wire back and forth
 * between the TX and RX programs: idle is listening (RX SM enabled, TX SM
 * claimed but parked), softSerialWriteByte() switches to TX on the first
 * queued byte, and the shared PIO IRQ handler switches back to RX
 * once the ring buffer is drained AND the TX SM reports fully stalled
 * (pio_sm_is_exec_stalled()) - i.e. the last frame, including its stop
 * bit, has actually finished shifting out, not just left the FIFO.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#if defined(USE_SOFTSERIAL1) || defined(USE_SOFTSERIAL2)

#include "build/build_config.h"

#include "common/utils.h"

#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/nvic.h"
#include "drivers/serial.h"
#include "drivers/serial_softserial.h"

#include "pg/serial_pinconfig.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#if defined(USE_SOFTSERIAL1) && defined(USE_SOFTSERIAL2)
#define MAX_SOFTSERIAL_PORTS 2
#else
#define MAX_SOFTSERIAL_PORTS 1
#endif

#define SOFTSERIAL_PIO_CYCLES_PER_BIT 8

typedef struct picoSoftSerial_s {
    serialPort_t port; // must be first
    IO_t rxIO;
    IO_t txIO;
    int8_t txSm; // -1 when direction unused
    int8_t rxSm;
    bool active;
    bool bidirTxActive; // BIDIR only: true while the shared pin is driven by the TX program
    volatile uint8_t rxBuffer[SOFTSERIAL_BUFFER_SIZE];
    volatile uint8_t txBuffer[SOFTSERIAL_BUFFER_SIZE];
} picoSoftSerial_t;

static const struct serialPortVTable picoSoftSerialVTable; // Forward

static picoSoftSerial_t softSerialPorts[MAX_SOFTSERIAL_PORTS];

static PIO softSerialPio;
static int txProgramOffset = -1;
static int rxProgramOffset = -1;
static bool softSerialIrqInstalled = false;
static int softSerialGpioBase = -1; // -1 = not decided yet

// A PIO block only addresses a 32-pin window (GPIO base 0..31 or 16..47,
// relevant on the 48-pin RP2350B/RP2354B). The base can only be chosen
// before the block's programs/SMs are set up, so the first port to open
// fixes it from its own pins; a later port's pins must fit the same
// window. tagTx/tagRx may be 0 (unused direction).
static bool softSerialClaimGpioBase(ioTag_t tagTx, ioTag_t tagRx)
{
    bool need16 = false;
    bool need0 = false;

    for (int i = 0; i < 2; i++) {
        const ioTag_t tag = i ? tagRx : tagTx;
        if (!tag) {
            continue;
        }
        const int pin = DEFIO_TAG_PIN(tag);
        need16 |= (pin >= 32);
        need0 |= (pin < 16);
    }

    if (need16 && need0) {
        bprintf("* softserial: pins span both PIO GPIO-base windows (<16 and >=32)");
        return false; // pins span more than one 32-pin window
    }

    if (softSerialGpioBase < 0) {
        softSerialGpioBase = need16 ? 16 : 0;
        pio_set_gpio_base(softSerialPio, softSerialGpioBase);
    }

    if ((need16 && softSerialGpioBase != 16) || (need0 && softSerialGpioBase != 0)) {
        bprintf("* softserial: pins outside the PIO GPIO-base %d window fixed by the first port",
                softSerialGpioBase);
        return false;
    }
    return true;
}

// --- PIO programs (pico-examples uart_tx.pio / uart_rx.pio, pioasm output) ---

static const uint16_t uart_tx_program_instructions[] = {
            //     .wrap_target
    0x9fa0, //  0: pull   block           side 1 [7]   ; stop bit (or idle-high wait)
    0xf727, //  1: set    x, 7            side 0 [7]   ; start bit
    0x6001, //  2: out    pins, 1                      ; data bits, LSB first
    0x0642, //  3: jmp    x--, 2                 [6]
            //     .wrap
};

static const struct pio_program uart_tx_program = {
    .instructions = uart_tx_program_instructions,
    .length = ARRAYLEN(uart_tx_program_instructions),
    .origin = -1,
    .pio_version = 0,
    .used_gpio_ranges = 0x0,
};

static const uint16_t uart_rx_program_instructions[] = {
            //     .wrap_target
    0x2020, //  0: wait   0 pin, 0                     ; start bit falling edge
    0xea27, //  1: set    x, 7                   [10]  ; centre of first data bit
    0x4001, //  2: in     pins, 1                      ; sample 8 bits...
    0x0642, //  3: jmp    x--, 2                 [6]   ; ...8 cycles apart
    0x00c8, //  4: jmp    pin, 8                       ; good stop bit?
    0xc014, //  5: irq    nowait 4 rel                 ; no: flag framing error...
    0x20a0, //  6: wait   1 pin, 0                     ; ...and wait for line idle
    0x0000, //  7: jmp    0
    0x8020, //  8: push   block                        ; byte (in ISR bits 31:24)
            //     .wrap
};

static const struct pio_program uart_rx_program = {
    .instructions = uart_rx_program_instructions,
    .length = ARRAYLEN(uart_rx_program_instructions),
    .origin = -1,
    .pio_version = 0,
    .used_gpio_ranges = 0x0,
};

static float softSerialClkdiv(uint32_t baud)
{
    return (float)clock_get_hz(clk_sys) / (SOFTSERIAL_PIO_CYCLES_PER_BIT * (float)baud);
}

static bool softSerialTxProgramInit(PIO pio, uint sm, uint pin, uint32_t baud)
{
    // Drive the pin to its idle level before handing it to PIO, so hooking
    // up doesn't glitch a start bit onto the wire.
    pio_sm_set_pins_with_mask64(pio, sm, 1ull << pin, 1ull << pin);
    pio_sm_set_pindirs_with_mask64(pio, sm, 1ull << pin, 1ull << pin);
    pio_gpio_init(pio, pin);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, txProgramOffset, txProgramOffset + ARRAYLEN(uart_tx_program_instructions) - 1);
    sm_config_set_sideset(&c, 2, true, false); // 1 side-set bit + enable bit
    // OUT shifts right (LSB first on the wire), no autopull - the program
    // pulls one FIFO word (one byte) per frame explicitly.
    sm_config_set_out_shift(&c, true, false, 32);
    // Both OUT (data bits) and side-set (start/stop bits) drive the TX pin.
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX); // TX only: 8-deep FIFO
    sm_config_set_clkdiv(&c, softSerialClkdiv(baud));
    // pio_sm_init() rejects (PICO_ERROR_BAD_ALIGNMENT) configs whose pins
    // fall outside the block's GPIO-base window - don't enable an SM whose
    // PINCTRL/EXECCTRL were never written.
    if (pio_sm_init(pio, sm, txProgramOffset, &c) != PICO_OK) {
        return false;
    }
    pio_sm_set_enabled(pio, sm, true);
    return true;
}

static bool softSerialRxProgramInit(PIO pio, uint sm, uint pin, uint32_t baud, bool inverted)
{
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, false);
    pio_gpio_init(pio, pin);
    // Bias the line to its idle level: physically high normally, physically
    // low for inverted protocols (the inover inverter below flips what the
    // SM sees, but the pull resistor acts on the real pad).
    gpio_set_pulls(pin, !inverted, inverted);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, rxProgramOffset, rxProgramOffset + ARRAYLEN(uart_rx_program_instructions) - 1);
    sm_config_set_in_pins(&c, pin);  // WAIT/IN
    sm_config_set_jmp_pin(&c, pin);  // JMP (stop-bit test)
    // Shift right, no autopush - the program pushes each complete byte.
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX); // RX only: 8-deep FIFO
    sm_config_set_clkdiv(&c, softSerialClkdiv(baud));
    if (pio_sm_init(pio, sm, rxProgramOffset, &c) != PICO_OK) {
        return false;
    }
    pio_sm_set_enabled(pio, sm, true);
    return true;
}

// --- SERIAL_BIDIR direction handover (shared pin, TX/RX programs take turns) ---

// Hand the shared wire from the RX program to the TX program. Called from
// softSerialWriteByte() the moment a write arrives while still listening.
static bool softSerialBidirSwitchToTx(picoSoftSerial_t *s)
{
    pio_sm_set_enabled(softSerialPio, s->rxSm, false);

    const uint pin = IO_Pin(s->txIO);
    if (!softSerialTxProgramInit(softSerialPio, s->txSm, pin, s->port.baudRate)) {
        return false;
    }
    if (s->port.options & SERIAL_INVERTED) {
        gpio_set_outover(pin, GPIO_OVERRIDE_INVERT);
    }
    s->bidirTxActive = true;
    return true;
}

// Hand the wire back to the RX program. Called from the PIO IRQ handler
// once the ring buffer is drained AND the TX SM is confirmed fully idle -
// see softSerialPioIrqHandler().
static void softSerialBidirSwitchToRx(picoSoftSerial_t *s)
{
    pio_sm_set_enabled(softSerialPio, s->txSm, false);

    const uint pin = IO_Pin(s->rxIO);
    softSerialRxProgramInit(softSerialPio, s->rxSm, pin, s->port.baudRate, (s->port.options & SERIAL_INVERTED) != 0);
    if (s->port.options & SERIAL_INVERTED) {
        gpio_set_inover(pin, GPIO_OVERRIDE_INVERT);
    }
    s->bidirTxActive = false;
}

// True once the TX SM has fully finished shifting out its last frame: the
// FIFO is empty AND the SM is parked back on the blocking `pull` at the
// program's first instruction, waiting for more data - i.e. the previous
// frame's data bits are completely off the wire and the line is at the
// stop/idle level. NOTE: pio_sm_is_exec_stalled() is NOT usable here - it
// reflects SMx_EXECCTRL.EXEC_STALLED, which only tracks instructions forced
// via SMx_INSTR (pio_sm_exec()), never a program's own `pull block` stall,
// so it would read 0 forever. Same PC-compare idiom as
// isSoftSerialTransmitBufferEmpty() below.
static bool softSerialTxFullyIdle(const picoSoftSerial_t *s)
{
    return pio_sm_is_tx_fifo_empty(softSerialPio, s->txSm)
        && pio_sm_get_pc(softSerialPio, s->txSm) == (uint)txProgramOffset;
}

// --- ring-buffer helpers (single producer / single consumer each way) ---

static uint32_t rxBytesUsed(const picoSoftSerial_t *s)
{
    return (s->port.rxBufferHead - s->port.rxBufferTail) & (SOFTSERIAL_BUFFER_SIZE - 1);
}

static uint32_t txBytesUsed(const picoSoftSerial_t *s)
{
    return (s->port.txBufferHead - s->port.txBufferTail) & (SOFTSERIAL_BUFFER_SIZE - 1);
}

// --- interrupt handler: RX drain + TX FIFO refill for every active port ---

static void softSerialPioIrqHandler(void)
{
    for (int i = 0; i < MAX_SOFTSERIAL_PORTS; i++) {
        picoSoftSerial_t *s = &softSerialPorts[i];
        if (!s->active) {
            continue;
        }

        if (s->rxSm >= 0) {
            while (!pio_sm_is_rx_fifo_empty(softSerialPio, s->rxSm)) {
                // RX program shifts right without autopush: byte is in bits 31:24.
                const uint8_t byte = (uint8_t)(pio_sm_get(softSerialPio, s->rxSm) >> 24);
                if (s->port.rxCallback) {
                    s->port.rxCallback(byte, s->port.rxCallbackData);
                } else {
                    s->port.rxBuffer[s->port.rxBufferHead] = byte;
                    s->port.rxBufferHead = (s->port.rxBufferHead + 1) & (SOFTSERIAL_BUFFER_SIZE - 1);
                }
            }
            // Clear a framing-error flag the RX program may have raised
            // (`irq nowait 4 rel`); it is informational only.
            pio_interrupt_clear(softSerialPio, (4 + s->rxSm) & 7);
        }

        if (s->txSm >= 0 && (!(s->port.options & SERIAL_BIDIR) || s->bidirTxActive)) {
            while (txBytesUsed(s) && !pio_sm_is_tx_fifo_full(softSerialPio, s->txSm)) {
                pio_sm_put(softSerialPio, s->txSm, s->port.txBuffer[s->port.txBufferTail]);
                s->port.txBufferTail = (s->port.txBufferTail + 1) & (SOFTSERIAL_BUFFER_SIZE - 1);
            }
            if (!txBytesUsed(s)) {
                const bool bidirTx = (s->port.options & SERIAL_BIDIR) && s->bidirTxActive;
                if (!bidirTx || softSerialTxFullyIdle(s)) {
                    // Ring drained (and, for BIDIR, the wire itself is
                    // confirmed idle): mask the (level-triggered)
                    // TX-FIFO-not-full source until more data is queued.
                    pio_set_irqn_source_enabled(softSerialPio, 0, pio_get_tx_fifo_not_full_interrupt_source(s->txSm), false);
                    if (bidirTx) {
                        softSerialBidirSwitchToRx(s);
                    }
                }
                // else: BIDIR, ring drained but the trailing frame is still
                // shifting out - leave the source enabled. It is
                // level-triggered on FIFO-empty (definitely true here), so
                // this handler is re-entered immediately; other active
                // ports' RX/TX are still serviced on every re-entry (the
                // loop above covers all of them), so this doesn't starve
                // them - it just re-checks softSerialTxFullyIdle() until the
                // last frame (bounded by a handful of bit periods) is done.
            }
        }
    }
}

// --- serialPort API (names match serial_softserial.h; the STM32
// timer-based serial_softserial.c is excluded from PICO builds) ---

void softSerialWriteByte(serialPort_t *instance, uint8_t ch)
{
    picoSoftSerial_t *s = (picoSoftSerial_t *)instance;

    if (s->txSm < 0 || !(s->port.mode & MODE_TX)) {
        return;
    }

    if (txBytesUsed(s) >= SOFTSERIAL_BUFFER_SIZE - 1) {
        return; // full - drop rather than block
    }

    // Queue the byte BEFORE any direction switch: once bidirTxActive is set,
    // the shared PIO IRQ handler (fired by any other enabled source) sees
    // "TX active + ring empty" and would immediately hand the wire back to
    // RX, clearing bidirTxActive again - after which the TX-FIFO-not-full
    // source unmasked below would never be serviced (the handler's TX branch
    // is gated on bidirTxActive), leaving a level-triggered IRQ storm and a
    // stranded byte. With the byte already in the ring, that early handover
    // can't trigger. (Same ordering as uartWrite() -> uartEnableTxInterrupt().)
    s->port.txBuffer[s->port.txBufferHead] = ch;
    s->port.txBufferHead = (s->port.txBufferHead + 1) & (SOFTSERIAL_BUFFER_SIZE - 1);

    if ((s->port.options & SERIAL_BIDIR) && !s->bidirTxActive) {
        if (!softSerialBidirSwitchToTx(s)) {
            // Roll the byte back out - the wire never left RX.
            s->port.txBufferHead = (s->port.txBufferHead - 1) & (SOFTSERIAL_BUFFER_SIZE - 1);
            return;
        }
    }

    // Kick the pump: unmasking TX-FIFO-not-full immediately takes the IRQ
    // (FIFO has room), which moves ring bytes into the FIFO.
    pio_set_irqn_source_enabled(softSerialPio, 0, pio_get_tx_fifo_not_full_interrupt_source(s->txSm), true);
}

uint32_t softSerialRxBytesWaiting(const serialPort_t *instance)
{
    const picoSoftSerial_t *s = (const picoSoftSerial_t *)instance;
    return rxBytesUsed(s);
}

uint32_t softSerialTxBytesFree(const serialPort_t *instance)
{
    const picoSoftSerial_t *s = (const picoSoftSerial_t *)instance;
    if (s->txSm < 0) {
        return 0;
    }
    return (SOFTSERIAL_BUFFER_SIZE - 1) - txBytesUsed(s);
}

uint8_t softSerialReadByte(serialPort_t *instance)
{
    picoSoftSerial_t *s = (picoSoftSerial_t *)instance;

    if (rxBytesUsed(s) == 0) {
        return 0;
    }

    const uint8_t byte = s->port.rxBuffer[s->port.rxBufferTail];
    s->port.rxBufferTail = (s->port.rxBufferTail + 1) & (SOFTSERIAL_BUFFER_SIZE - 1);
    return byte;
}

// Stop an SM, flush its FIFOs, apply a new divider and restart it cleanly
// from its program's first instruction. Restarting mid-frame would corrupt
// the byte in flight (and bytes queued at the old rate would go out at the
// new one), so the SM is fully quiesced around the change.
static void softSerialRestartSm(uint sm, int programOffset, float div)
{
    pio_sm_set_enabled(softSerialPio, sm, false);
    pio_sm_clear_fifos(softSerialPio, sm);
    pio_sm_set_clkdiv(softSerialPio, sm, div);
    pio_sm_restart(softSerialPio, sm);
    pio_sm_clkdiv_restart(softSerialPio, sm);
    pio_sm_exec(softSerialPio, sm, pio_encode_jmp(programOffset));
    pio_sm_set_enabled(softSerialPio, sm, true);
}

void softSerialSetBaudRate(serialPort_t *instance, uint32_t baudRate)
{
    picoSoftSerial_t *s = (picoSoftSerial_t *)instance;
    const float div = softSerialClkdiv(baudRate);

    if (s->port.options & SERIAL_BIDIR) {
        // Only one of the two SMs is actually enabled at a time in BIDIR
        // mode - restarting+re-enabling the idle one would wrongly power it
        // up and drive/listen on the shared pin alongside the active one.
        if (s->bidirTxActive) {
            softSerialRestartSm(s->txSm, txProgramOffset, div);
        } else if (s->rxSm >= 0) {
            softSerialRestartSm(s->rxSm, rxProgramOffset, div);
        }
    } else {
        if (s->txSm >= 0) {
            softSerialRestartSm(s->txSm, txProgramOffset, div);
        }
        if (s->rxSm >= 0) {
            softSerialRestartSm(s->rxSm, rxProgramOffset, div);
        }
    }
    s->port.baudRate = baudRate;
}

bool isSoftSerialTransmitBufferEmpty(const serialPort_t *instance)
{
    const picoSoftSerial_t *s = (const picoSoftSerial_t *)instance;
    if (s->txSm < 0) {
        return true;
    }
    if ((s->port.options & SERIAL_BIDIR) && !s->bidirTxActive) {
        // Already handed the wire back to the receiver - and the TX SM may
        // never have been pio_sm_init()'d yet in this session, so its PC
        // reads as whatever the hardware defaults to, not txProgramOffset.
        return true;
    }
    // The FIFO empties as soon as the SM has PULLed the last word - the
    // frame still needs ~10 bit times to shift out. The SM is only truly
    // idle when it is back parked on the blocking `pull` at the program's
    // first instruction (callers use this as a "safe to reconfigure /
    // release the line" gate, so reporting early truncates the last byte).
    return txBytesUsed(s) == 0
        && pio_sm_is_tx_fifo_empty(softSerialPio, s->txSm)
        && pio_sm_get_pc(softSerialPio, s->txSm) == (uint)txProgramOffset;
}

static void softSerialSetMode(serialPort_t *instance, portMode_e mode)
{
    instance->mode = mode;
}

serialPort_t *openSoftSerial(softSerialPortIndex_e portIndex, serialReceiveCallbackPtr rxCallback, void *rxCallbackData, uint32_t baud, portMode_e mode, portOptions_e options)
{
    if (portIndex >= MAX_SOFTSERIAL_PORTS) {
        return NULL;
    }

    const bool bidir = (options & SERIAL_BIDIR) != 0;

    picoSoftSerial_t *s = &softSerialPorts[portIndex];
    if (s->active) {
        return NULL;
    }

    const int pinCfgIndex = portIndex + RESOURCE_SOFT_OFFSET;
    const ioTag_t tagRx = serialPinConfig()->ioTagRx[pinCfgIndex];
    const ioTag_t tagTx = serialPinConfig()->ioTagTx[pinCfgIndex];

    if (bidir) {
        // Single-wire half duplex: only the TX pin is used (matches the
        // STM32 half-duplex convention in serial_softserial.c) - both
        // directions are mandatory, since there'd be nothing to hand the
        // wire back and forth between otherwise.
        if ((mode & MODE_RXTX) != MODE_RXTX || !tagTx) {
            return NULL;
        }
    } else {
        if (((mode & MODE_RX) && !tagRx) || ((mode & MODE_TX) && !tagTx)) {
            return NULL;
        }

        if ((mode & MODE_RXTX) == MODE_RXTX && tagRx == tagTx) {
            // Pin directions are per-pin, not per-state-machine: the RX SM
            // init would switch the shared pin to input and silently break
            // TX. Full duplex on one pin makes no sense anyway - use
            // SERIAL_BIDIR for a single-wire port.
            return NULL;
        }
    }

    softSerialPio = PIO_INSTANCE(PIO_UART_INDEX);

    if (!softSerialClaimGpioBase(bidir ? tagTx : ((mode & MODE_TX) ? tagTx : IO_TAG_NONE),
                                 bidir ? IO_TAG_NONE : ((mode & MODE_RX) ? tagRx : IO_TAG_NONE))) {
        return NULL;
    }

    // Load each program once, shared by both ports' state machines.
    if (txProgramOffset < 0 && (mode & MODE_TX)) {
        if (!pio_can_add_program(softSerialPio, &uart_tx_program)) {
            return NULL;
        }
        txProgramOffset = pio_add_program(softSerialPio, &uart_tx_program);
    }
    if (rxProgramOffset < 0 && (mode & MODE_RX)) {
        if (!pio_can_add_program(softSerialPio, &uart_rx_program)) {
            return NULL;
        }
        rxProgramOffset = pio_add_program(softSerialPio, &uart_rx_program);
    }

    memset((void *)s->rxBuffer, 0, sizeof(s->rxBuffer));
    memset((void *)s->txBuffer, 0, sizeof(s->txBuffer));

    s->port.vTable = &picoSoftSerialVTable;
    s->port.baudRate = baud;
    s->port.mode = mode;
    s->port.options = options;
    s->port.rxCallback = rxCallback;
    s->port.rxCallbackData = rxCallbackData;
    s->port.rxBuffer = s->rxBuffer;
    s->port.txBuffer = s->txBuffer;
    s->port.rxBufferSize = SOFTSERIAL_BUFFER_SIZE;
    s->port.txBufferSize = SOFTSERIAL_BUFFER_SIZE;
    s->port.rxBufferHead = s->port.rxBufferTail = 0;
    s->port.txBufferHead = s->port.txBufferTail = 0;
    s->txSm = -1;
    s->rxSm = -1;
    s->bidirTxActive = false;

    if (mode & MODE_TX) {
        const int sm = pio_claim_unused_sm(softSerialPio, false);
        if (sm < 0) {
            return NULL;
        }
        s->txSm = (int8_t)sm;
        s->txIO = IOGetByTag(tagTx);
        IOInit(s->txIO, OWNER_SERIAL_TX, RESOURCE_INDEX(pinCfgIndex));

        if (!bidir) {
            const uint txPin = IO_Pin(s->txIO);
            if (!softSerialTxProgramInit(softSerialPio, sm, txPin, baud)) {
                pio_sm_unclaim(softSerialPio, sm);
                s->txSm = -1;
                IORelease(s->txIO); // don't leave the pin marked owned on a failed open
                return NULL;
            }
            if (options & SERIAL_INVERTED) {
                gpio_set_outover(txPin, GPIO_OVERRIDE_INVERT);
            }
        }
        // bidir: the SM is claimed but left uninitialized/disabled - idle
        // state is listening, so the RX program owns the pin until the
        // first write hands it over (see softSerialBidirSwitchToTx()).
    }

    if (mode & MODE_RX) {
        const int sm = pio_claim_unused_sm(softSerialPio, false);
        bool rxOk = sm >= 0;
        if (rxOk) {
            s->rxSm = (int8_t)sm;
            s->rxIO = bidir ? s->txIO : IOGetByTag(tagRx);
            if (!bidir) {
                IOInit(s->rxIO, OWNER_SERIAL_RX, RESOURCE_INDEX(pinCfgIndex));
            }

            const uint rxPin = IO_Pin(s->rxIO);
            rxOk = softSerialRxProgramInit(softSerialPio, sm, rxPin, baud, (options & SERIAL_INVERTED) != 0);
            if (rxOk && (options & SERIAL_INVERTED)) {
                gpio_set_inover(rxPin, GPIO_OVERRIDE_INVERT);
            }
        }
        if (!rxOk) {
            if (sm >= 0) {
                pio_sm_unclaim(softSerialPio, sm);
                s->rxSm = -1;
                if (!bidir) {
                    IORelease(s->rxIO);
                }
            }
            if (s->txSm >= 0) {
                pio_sm_set_enabled(softSerialPio, s->txSm, false);
                pio_sm_unclaim(softSerialPio, s->txSm);
                s->txSm = -1;
                IORelease(s->txIO); // don't leave pins marked owned on a failed open
            }
            return NULL;
        }
    }

    // Shared IRQ for the whole PIO block: RX sources stay enabled for the
    // port's lifetime; TX sources are toggled by the write path.
    if (!softSerialIrqInstalled) {
        const uint irqNum = pio_get_irq_num(softSerialPio, 0);
        irq_set_exclusive_handler(irqNum, softSerialPioIrqHandler);
        irq_set_enabled(irqNum, true);
        softSerialIrqInstalled = true;
    }

    // Mark active BEFORE unmasking the (level-triggered) RX source: the RX
    // SM has been running since its init, so a byte could already be in the
    // FIFO - if the IRQ fired first, the handler's !active skip would leave
    // the level source asserted and the core would tail-chain back into the
    // handler forever. Also flush any line noise captured during init so it
    // isn't delivered as a frame.
    s->active = true;
    if (s->rxSm >= 0) {
        pio_sm_clear_fifos(softSerialPio, s->rxSm);
        pio_set_irqn_source_enabled(softSerialPio, 0, pio_get_rx_fifo_not_empty_interrupt_source(s->rxSm), true);
    }

    return &s->port;
}

static const struct serialPortVTable picoSoftSerialVTable = {
    .serialWrite = softSerialWriteByte,
    .serialTotalRxWaiting = softSerialRxBytesWaiting,
    .serialTotalTxFree = softSerialTxBytesFree,
    .serialRead = softSerialReadByte,
    .serialSetBaudRate = softSerialSetBaudRate,
    .isSerialTransmitBufferEmpty = isSoftSerialTransmitBufferEmpty,
    .setMode = softSerialSetMode,
    .setCtrlLineStateCb = NULL,
    .setBaudRateCb = NULL,
    .writeBuf = NULL,
    .beginWrite = NULL,
    .endWrite = NULL
};

#endif // USE_SOFTSERIAL1 || USE_SOFTSERIAL2
