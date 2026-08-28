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
 * RP2350/RP2354 hardware UART driver.
 *
 * Adapted from betaflight's src/platform/PICO/uart/{serial_uart_pico,
 * uart_hw}.c against Wingflight's OLDER/existing UART driver API
 * (drivers/serial_uart.h, serial_uart_impl.h - uartDevmap[]/uartHardware_t
 * with rxPin/txPin/rx/tx fields, serialUART(UARTDevice_e device, ...)), NOT
 * betaflight's newer container_of()-based flat uartDevice_t[] architecture.
 *
 * Scope: hardware UARTs only (uart0/uart1). PIO-based extra UARTs are
 * implemented separately as SOFTSERIAL1/2 (drivers/serial_softserial_pico.c),
 * not as betaflight-style additional hardware-UART numbers (uart_pio.c). The
 * TX-line half-duplex monitor feature (SERIAL_CHECK_TX/checkUsartTxOutput,
 * betaflight-only, not present in Wingflight's serial_uart.h) is not ported.
 *
 * SERIAL_BIDIR: unlike STM32's USART (a real single-wire half-duplex
 * peripheral mode, HAL_HalfDuplex_Init), the RP2350's PL011 UART has no such
 * mode, and a UART instance's TX and RX pins are always fixed, distinct
 * GPIOs (see uartHardware[]'s rxPins[]/txPins[] tables) - one GPIO can never
 * serve as both. So single-wire half duplex on a hardware UART port here
 * requires the board to physically tie the instance's TX and RX pins
 * together on the same net; the driver just switches which of the two pins
 * is electrically live (UART function enabled and driving/listening) vs.
 * released to Hi-Z, starting out listening (RX pin live) and handing the
 * wire over to TX on the first queued byte, then back to RX once the
 * hardware UARTFR.BUSY flag confirms the last frame has actually finished
 * shifting out (see uartBidirSwitchToTx()/uartBidirSwitchToRx()).
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_UART

#include "drivers/io.h"
#include "drivers/nvic.h"
#include "drivers/resource.h"
#include "drivers/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_uart_impl.h"

#include "pg/serial_pinconfig.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "hardware/structs/uart.h"

const uartHardware_t uartHardware[] = {
#ifdef USE_UART1
    {
        .device = UARTDEV_1,
        .reg = (USART_TypeDef *)uart0,
        .rxPins = {
            { DEFIO_TAG_E(PA1) },
            { DEFIO_TAG_E(PA3) },
            { DEFIO_TAG_E(PA13) },
            { DEFIO_TAG_E(PA15) },
            { DEFIO_TAG_E(PA17) },
            { DEFIO_TAG_E(PA19) },
            { DEFIO_TAG_E(PA29) },
#if defined(RP2350B) || defined(RP2354B)
            { DEFIO_TAG_E(PA31) },
            { DEFIO_TAG_E(PA33) },
            { DEFIO_TAG_E(PA35) },
            { DEFIO_TAG_E(PA45) },
            { DEFIO_TAG_E(PA47) },
#endif
        },
        .txPins = {
            { DEFIO_TAG_E(PA0) },
            { DEFIO_TAG_E(PA2) },
            { DEFIO_TAG_E(PA12) },
            { DEFIO_TAG_E(PA14) },
            { DEFIO_TAG_E(PA16) },
            { DEFIO_TAG_E(PA18) },
            { DEFIO_TAG_E(PA28) },
#if defined(RP2350B) || defined(RP2354B)
            { DEFIO_TAG_E(PA30) },
            { DEFIO_TAG_E(PA32) },
            { DEFIO_TAG_E(PA34) },
            { DEFIO_TAG_E(PA44) },
            { DEFIO_TAG_E(PA46) },
#endif
        },
        .irqn = UART0_IRQ,
        .txPriority = NVIC_PRIO_SERIALUART1,
        .rxPriority = NVIC_PRIO_SERIALUART1,
        .txBuffer = uart1TxBuffer,
        .rxBuffer = uart1RxBuffer,
        .txBufferSize = sizeof(uart1TxBuffer),
        .rxBufferSize = sizeof(uart1RxBuffer),
    },
#endif

#ifdef USE_UART2
    {
        .device = UARTDEV_2,
        .reg = (USART_TypeDef *)uart1,
        .rxPins = {
            { DEFIO_TAG_E(PA5) },
            { DEFIO_TAG_E(PA7) },
            { DEFIO_TAG_E(PA9) },
            { DEFIO_TAG_E(PA11) },
            { DEFIO_TAG_E(PA21) },
            { DEFIO_TAG_E(PA23) },
            { DEFIO_TAG_E(PA25) },
            { DEFIO_TAG_E(PA27) },
#if defined(RP2350B) || defined(RP2354B)
            { DEFIO_TAG_E(PA37) },
            { DEFIO_TAG_E(PA39) },
            { DEFIO_TAG_E(PA41) },
            { DEFIO_TAG_E(PA43) },
#endif
        },
        .txPins = {
            { DEFIO_TAG_E(PA4) },
            { DEFIO_TAG_E(PA6) },
            { DEFIO_TAG_E(PA8) },
            { DEFIO_TAG_E(PA10) },
            { DEFIO_TAG_E(PA20) },
            { DEFIO_TAG_E(PA22) },
            { DEFIO_TAG_E(PA24) },
            { DEFIO_TAG_E(PA26) },
#if defined(RP2350B) || defined(RP2354B)
            { DEFIO_TAG_E(PA36) },
            { DEFIO_TAG_E(PA38) },
            { DEFIO_TAG_E(PA40) },
            { DEFIO_TAG_E(PA42) },
#endif
        },
        .irqn = UART1_IRQ,
        .txPriority = NVIC_PRIO_SERIALUART2,
        .rxPriority = NVIC_PRIO_SERIALUART2,
        .txBuffer = uart2TxBuffer,
        .rxBuffer = uart2RxBuffer,
        .txBufferSize = sizeof(uart2TxBuffer),
        .rxBufferSize = sizeof(uart2RxBuffer),
    },
#endif
};

// uartSelectPins() only latches the previously-resolved uartPinConfigure()
// choice (uartHardware_t rxPins[]/txPins[] -> uartDevice_t rxPin/txPin) into
// the "active" rx/tx fields - identical, MCU-agnostic logic to
// serial_uart_stdperiph.c's version (not compiled for PICO, so duplicated
// here instead of shared).
void uartSelectPins(UARTDevice_e device, portOptions_e options)
{
    UNUSED(options);

    uartDevice_t *uartDevice = uartDevmap[device];

    if (uartDevice) {
        uartDevice->rx = uartDevice->rxPin;
        uartDevice->tx = uartDevice->txPin;
    }
}

// --- SERIAL_BIDIR direction handover state, one slot per uartDevmap[] entry ---

typedef struct picoUartBidir_s {
    bool active;   // this device was opened with SERIAL_BIDIR
    bool txActive; // true: TX pin is live (driving); false: RX pin is live (listening)
    uint32_t txPin;
    uint32_t rxPin;
    portOptions_e options;
} picoUartBidir_t;

static picoUartBidir_t uartBidir[UARTDEV_COUNT_MAX];

#ifdef USE_SERIAL_BIDIR_SWITCH
// serialUART() indexes ioTagBidirEnable[] by UARTDevice_e.
STATIC_ASSERT(SERIAL_BIDIR_SWITCH_COUNT >= UARTDEV_COUNT_MAX, serial_bidir_switch_count_too_small);
#endif

static int uartDeviceIndex(const uartPort_t *uartPort)
{
    for (int i = 0; i < UARTDEV_COUNT_MAX; i++) {
        if (uartDevmap[i] && &uartDevmap[i]->port == uartPort) {
            return i;
        }
    }
    return -1;
}

// Detach a pin from the UART peripheral and float it - the released side of
// a half-duplex pair must not drive (or be read from) the shared wire while
// the other side owns it.
static void uartBidirReleasePin(uint32_t pin)
{
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, false);
    gpio_set_pulls(pin, false, false);
}

static void uartBidirSwitchToTx(int device)
{
    picoUartBidir_t *b = &uartBidir[device];
    if (b->txActive) {
        return;
    }

    uart_inst_t *uartInstance = UART_INST(uartDevmap[device]->port.USARTx);

    // Mute the receiver before driving the shared wire, so our own
    // transmission isn't echoed back up as bogus RX bytes.
    uart_set_irqs_enabled(uartInstance, false, false);
    uartBidirReleasePin(b->rxPin);

    gpio_set_function(b->txPin, UART_FUNCSEL_NUM(uartInstance, b->txPin));
    gpio_set_pulls(b->txPin, false, false);
    if (b->options & SERIAL_INVERTED) {
        gpio_set_outover(b->txPin, GPIO_OVERRIDE_INVERT);
    }

    b->txActive = true;
}

static void uartBidirSwitchToRx(int device)
{
    picoUartBidir_t *b = &uartBidir[device];
    if (!b->txActive) {
        return;
    }

    uart_inst_t *uartInstance = UART_INST(uartDevmap[device]->port.USARTx);

    uartBidirReleasePin(b->txPin);

    gpio_set_function(b->rxPin, UART_FUNCSEL_NUM(uartInstance, b->rxPin));
    if (b->options & SERIAL_INVERTED) {
        gpio_set_pulls(b->rxPin, false, true);
        gpio_set_inover(b->rxPin, GPIO_OVERRIDE_INVERT);
    } else {
        gpio_set_pulls(b->rxPin, true, false);
    }

    // Discard whatever the floating/detached pin fed into the RX FIFO while
    // it was disconnected from the peripheral, before letting it interrupt
    // again - that noise isn't a real frame.
    while (uart_is_readable(uartInstance)) {
        (void)uart_get_hw(uartInstance)->dr;
    }
    uart_set_irqs_enabled(uartInstance, true, false);

    b->txActive = false;
}

static void sendBufferToUART(uartPort_t *s)
{
    uart_inst_t *uartInstance = UART_INST(s->USARTx);
    uart_hw_t *uartHw = uart_get_hw(uartInstance);

    // Fill the TX FIFO; an interrupt fires again once the FIFO empties below threshold.
    while (uart_is_writable(uartInstance)) {
        if (s->port.txBufferTail != s->port.txBufferHead) {
            uartHw->dr = s->port.txBuffer[s->port.txBufferTail];
            s->port.txBufferTail = (s->port.txBufferTail + 1) % s->port.txBufferSize;
        } else {
            hw_clear_bits(&(uartHw->imsc), UART_UARTIMSC_TXIM_BITS);
            break;
        }
    }
}

static void uartIrqHandler_pico(UARTDevice_e device)
{
    uartPort_t *s = &(uartDevmap[device]->port);
    uart_inst_t *uartInstance = UART_INST(s->USARTx);
    uart_hw_t *uartHw = uart_get_hw(uartInstance);
    const uint32_t misr = uartHw->mis; // Masked interrupt status

    if ((misr & (UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS)) != 0 &&
        (uartHw->imsc & (UART_UARTIMSC_RXIM_BITS | UART_UARTIMSC_RTIM_BITS)) != 0) {
        while (uart_is_readable(uartInstance)) {
            const uint8_t ch = uartHw->dr;
            if (s->port.rxCallback) {
                s->port.rxCallback(ch, s->port.rxCallbackData);
            } else {
                s->port.rxBuffer[s->port.rxBufferHead] = ch;
                s->port.rxBufferHead = (s->port.rxBufferHead + 1) % s->port.rxBufferSize;
            }
        }
    }

    if ((misr & UART_UARTIMSC_TXIM_BITS) != 0 && (uartHw->imsc & UART_UARTIMSC_TXIM_BITS) != 0) {
        sendBufferToUART(s);

        picoUartBidir_t *b = &uartBidir[device];
        if (b->active && b->txActive && s->port.txBufferTail == s->port.txBufferHead) {
            if (uartHw->fr & UART_UARTFR_BUSY_BITS) {
                // Ring drained but the last frame is still shifting out of
                // the hardware FIFO/shift register - sendBufferToUART() just
                // masked TXIM (nothing left to feed it), so re-arm it purely
                // as a retrigger: it's level-triggered on FIFO-below-
                // threshold, true with an empty FIFO, so this handler is
                // called again immediately and re-checks BUSY. Bounded by
                // however long the last few queued bytes take at this baud.
                hw_set_bits(&(uartHw->imsc), UART_UARTIMSC_TXIM_BITS);
            } else {
                uartBidirSwitchToRx(device);
            }
        }
    }
}

static void onUart0Irq(void)
{
    uartIrqHandler_pico(UARTDEV_1);
}

static void onUart1Irq(void)
{
    uartIrqHandler_pico(UARTDEV_2);
}

uartPort_t *serialUART(UARTDevice_e device, uint32_t baudRate, portMode_e mode, portOptions_e options)
{
    uartDevice_t *uart = uartDevmap[device];
    if (!uart) {
        return NULL;
    }

    const uartHardware_t *hardware = uart->hardware;
    if (!hardware) {
        return NULL;
    }

    const bool bidir = (options & SERIAL_BIDIR) != 0;
    if (bidir && (mode & MODE_RXTX) != MODE_RXTX) {
        // Nothing to hand the wire back and forth between otherwise.
        return NULL;
    }

    uartPort_t *s = &(uart->port);
    s->port.vTable = uartVTable;

    s->port.baudRate = baudRate;

    s->port.rxBuffer = hardware->rxBuffer;
    s->port.txBuffer = hardware->txBuffer;
    s->port.rxBufferSize = hardware->rxBufferSize;
    s->port.txBufferSize = hardware->txBufferSize;

    s->USARTx = hardware->reg;

    uart_inst_t *uartInstance = UART_INST(hardware->reg);

    IO_t txIO = IOGetByTag(uart->tx.pin);
    IO_t rxIO = IOGetByTag(uart->rx.pin);

    picoUartBidir_t *b = &uartBidir[device];
    memset(b, 0, sizeof(*b));

    if (bidir) {
        // Both the instance's fixed TX and RX pins are needed - the board
        // must tie them together externally (see the file-header comment).
        if (!txIO || !rxIO) {
            return NULL;
        }

        b->active = true;
        b->txPin = IO_Pin(txIO);
        b->rxPin = IO_Pin(rxIO);
        b->options = options;

        IOInit(txIO, OWNER_SERIAL_TX, RESOURCE_INDEX(device));
        IOInit(rxIO, OWNER_SERIAL_RX, RESOURCE_INDEX(device));

        // Idle state is listening: release the TX pin to Hi-Z and bring up
        // only the RX pin, mirroring softSerial's BIDIR default.
        uartBidirReleasePin(b->txPin);

        gpio_set_function(b->rxPin, UART_FUNCSEL_NUM(uartInstance, b->rxPin));
        if (options & SERIAL_INVERTED) {
            gpio_set_pulls(b->rxPin, false, true); // pull down
        } else {
            gpio_set_pulls(b->rxPin, true, false); // pull up
        }

#ifdef USE_SERIAL_BIDIR_SWITCH
        // Drive the external SPST combiner switch's enable pin (if the
        // board has one - resource SERIAL_BIDIR_EN) closed for the whole
        // time this port is open in half duplex. Boards without the switch
        // just leave this unset and rely on a bare wire-tie instead.
        IO_t bidirEnIO = IOGetByTag(serialPinConfig()->ioTagBidirEnable[device]);
        if (bidirEnIO) {
            IOInit(bidirEnIO, OWNER_SERIAL_BIDIR_ENABLE, RESOURCE_INDEX(device));
            IOConfigGPIO(bidirEnIO, IOCFG_OUT_PP);
            IOWrite(bidirEnIO, true);
        }
#endif
    } else {
#ifdef USE_SERIAL_BIDIR_SWITCH
        // Full duplex on this port: if a combiner switch is fitted, hold it
        // open so the two pins stay isolated (matters if the same board
        // config is ever reused without SERIAL_BIDIR set).
        IO_t bidirEnIO = IOGetByTag(serialPinConfig()->ioTagBidirEnable[device]);
        if (bidirEnIO) {
            IOInit(bidirEnIO, OWNER_SERIAL_BIDIR_ENABLE, RESOURCE_INDEX(device));
            IOConfigGPIO(bidirEnIO, IOCFG_OUT_PP);
            IOWrite(bidirEnIO, false);
        }
#endif
        if ((mode & MODE_TX) && txIO) {
            IOInit(txIO, OWNER_SERIAL_TX, RESOURCE_INDEX(device));
            const uint32_t txPin = IO_Pin(txIO);
            gpio_set_function(txPin, UART_FUNCSEL_NUM(uartInstance, txPin));
            gpio_set_pulls(txPin, false, false);
        }

        if ((mode & MODE_RX) && rxIO) {
            IOInit(rxIO, OWNER_SERIAL_RX, RESOURCE_INDEX(device));
            const uint32_t rxPin = IO_Pin(rxIO);
            gpio_set_function(rxPin, UART_FUNCSEL_NUM(uartInstance, rxPin));
            if (options & SERIAL_INVERTED) {
                gpio_set_pulls(rxPin, false, true); // pull down
            } else {
                gpio_set_pulls(rxPin, true, false); // pull up
            }
        }
    }

    uart_init(uartInstance, baudRate);
    uart_set_hw_flow(uartInstance, false, false);
    uart_set_format(uartInstance, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uartInstance, true);

    irq_set_exclusive_handler(hardware->irqn, hardware->irqn == UART0_IRQ ? onUart0Irq : onUart1Irq);
    irq_set_enabled(hardware->irqn, true);

    if (bidir) {
        // Only the currently-live side (RX, at open) needs its override set
        // now - uartBidirSwitchToTx()/ToRx() apply the matching override
        // each time they hand the wire over from here on.
        if (options & SERIAL_INVERTED) {
            gpio_set_inover(b->rxPin, GPIO_OVERRIDE_INVERT);
        }
    } else if (options & SERIAL_INVERTED) {
        if (rxIO) {
            gpio_set_inover(IO_Pin(rxIO), GPIO_OVERRIDE_INVERT);
        }
        if (txIO) {
            gpio_set_outover(IO_Pin(txIO), GPIO_OVERRIDE_INVERT);
        }
    }

    // Don't enable RX IRQ yet - uartOpen() calls uartReconfigure() right after
    // this, once rxCallback has been set.
    return s;
}

void uartReconfigure(uartPort_t *uartPort)
{
    uart_inst_t *uartInstance = UART_INST(uartPort->USARTx);

    uart_init(uartInstance, uartPort->port.baudRate);

    const bool twoStop = uartPort->port.options & SERIAL_STOPBITS_2;
    const bool evenParity = uartPort->port.options & SERIAL_PARITY_EVEN;
    uart_set_format(uartInstance, 8, twoStop ? 2 : 1, evenParity ? UART_PARITY_EVEN : UART_PARITY_NONE);
    uart_set_fifo_enabled(uartInstance, true);
    uart_set_hw_flow(uartInstance, false, false);

    // uart_init() cleared IMSC, so restore the mask matching the port's
    // current direction. For a BIDIR port that is mid-transmission (e.g. a
    // baud change while draining), the RX pin is detached/Hi-Z - enabling RX
    // interrupts would only harvest noise, and dropping TXIM would strand the
    // remaining ring bytes with txActive stuck; re-arm the TX pump instead
    // and let the IRQ handler hand the wire back to RX as usual.
    const int device = uartDeviceIndex(uartPort);
    const bool bidirTx = device >= 0 && uartBidir[device].active && uartBidir[device].txActive;

    if (bidirTx) {
        hw_set_bits(&(uart_get_hw(uartInstance)->imsc), UART_UARTIMSC_TXIM_BITS);
    } else if (uartPort->port.mode & MODE_RX) {
        uart_set_irqs_enabled(uartInstance, true, false);
    }
}

void uartEnableTxInterrupt(uartPort_t *uartPort)
{
    if (uartPort->port.txBufferTail == uartPort->port.txBufferHead) {
        // Nothing queued: arming the level-triggered TXIM here would just
        // cost one spurious IRQ that immediately disarms itself.
        return;
    }

    const int device = uartDeviceIndex(uartPort);
    if (device >= 0 && uartBidir[device].active && !uartBidir[device].txActive) {
        uartBidirSwitchToTx(device);
    }

    uart_inst_t *uartInstance = UART_INST(uartPort->USARTx);
    uart_hw_t *uartHw = uart_get_hw(uartInstance);

    // Temporarily disable the TX interrupt mask so sendBufferToUART() below
    // can't race with the IRQ handler also calling it.
    hw_clear_bits(&(uartHw->imsc), UART_UARTIMSC_TXIM_BITS);
    sendBufferToUART(uartPort);
    hw_set_bits(&(uartHw->imsc), UART_UARTIMSC_TXIM_BITS);
}

void uartTryStartTxDMA(uartPort_t *s)
{
    UNUSED(s);
}

#endif // USE_UART
