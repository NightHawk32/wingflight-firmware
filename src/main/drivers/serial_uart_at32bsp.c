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
 * AT-BSP (Artery AT32F435/437) UART logic driver.
 *
 * Combines what betaflight splits into serial_uart_at32bsp.c (logic, newer
 * container_of/TX_PIN_MONITOR abstraction) into a single file matching
 * Wingflight's older uartPort_t/uartHardware_t contract, following the same
 * "one file per MCU family" convention already used by bus_spi_at32bsp.c and
 * bus_i2c_at32bsp.c.
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef USE_UART

#include "build/atomic.h"

#include "common/utils.h"

#include "drivers/dma.h"
#include "drivers/inverter.h"
#include "drivers/io.h"
#include "drivers/nvic.h"
#include "drivers/rcc.h"

#include "drivers/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_uart_impl.h"

static void usartConfigurePinInversion(uartPort_t *uartPort)
{
#if !defined(USE_INVERTER)
    UNUSED(uartPort);
#else
    bool inverted = uartPort->port.options & SERIAL_INVERTED;

    if (inverted) {
        // Enable hardware inverter if available (AT-BSP's usart_type has no
        // built-in RX/TX signal inversion register, only external inverter support).
        enableInverter(uartPort->USARTx, true);
    }
#endif
}

void uartSelectPins(UARTDevice_e device, portOptions_e options)
{
    UNUSED(options);

    uartDevice_t *uartDevice = uartDevmap[device];

    if (uartDevice) {
        uartDevice->rx = uartDevice->rxPin;
        uartDevice->tx = uartDevice->txPin;
    }
}

void uartReconfigure(uartPort_t *uartPort)
{
    usart_enable(uartPort->USARTx, DISABLE);

    // AT-BSP's usart_init() bundles baudrate/data-bits/stop-bits together in one call;
    // parity, flow control, direction, and half-duplex are configured separately below.
    // According to StdPeriph-style convention, word length must include the parity bit.
    usart_init(uartPort->USARTx, uartPort->port.baudRate,
        (uartPort->port.options & SERIAL_PARITY_EVEN) ? USART_DATA_9BITS : USART_DATA_8BITS,
        (uartPort->port.options & SERIAL_STOPBITS_2) ? USART_STOP_2_BIT : USART_STOP_1_BIT);

    usart_parity_selection_config(uartPort->USARTx, (uartPort->port.options & SERIAL_PARITY_EVEN) ? USART_PARITY_EVEN : USART_PARITY_NONE);

    usart_hardware_flow_control_set(uartPort->USARTx, USART_HARDWARE_FLOW_NONE);

    usart_transmitter_enable(uartPort->USARTx, (uartPort->port.mode & MODE_TX) ? ENABLE : DISABLE);
    usart_receiver_enable(uartPort->USARTx, (uartPort->port.mode & MODE_RX) ? ENABLE : DISABLE);

    usartConfigurePinInversion(uartPort);

    usart_single_line_halfduplex_select(uartPort->USARTx, (uartPort->port.options & SERIAL_BIDIR) ? ENABLE : DISABLE);

    usart_enable(uartPort->USARTx, ENABLE);

    // Receive DMA or IRQ
    if (uartPort->port.mode & MODE_RX) {
        if (uartPort->rxDMAResource) {
            dma_init_type dmaInit;
            dma_default_para_init(&dmaInit);
            dmaInit.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
            dmaInit.loop_mode_enable = TRUE;
            dmaInit.peripheral_base_addr = uartPort->rxDMAPeripheralBaseAddr;
            dmaInit.priority = DMA_PRIORITY_MEDIUM;
            dmaInit.peripheral_inc_enable = FALSE;
            dmaInit.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
            dmaInit.memory_inc_enable = TRUE;
            dmaInit.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
            dmaInit.buffer_size = uartPort->port.rxBufferSize;
            dmaInit.memory_base_addr = (uint32_t)uartPort->port.rxBuffer;

            xDMA_DeInit(uartPort->rxDMAResource);
            xDMA_Init(uartPort->rxDMAResource, &dmaInit);
            xDMA_Cmd(uartPort->rxDMAResource, ENABLE);
            usart_dma_receiver_enable(uartPort->USARTx, ENABLE);
            uartPort->rxDMAPos = xDMA_GetCurrDataCounter(uartPort->rxDMAResource);
        } else {
            usart_flag_clear(uartPort->USARTx, USART_RDBF_FLAG);
            usart_interrupt_enable(uartPort->USARTx, USART_RDBF_INT, ENABLE);
            usart_interrupt_enable(uartPort->USARTx, USART_IDLE_INT, ENABLE);
        }
    }

    // Transmit DMA or IRQ
    if (uartPort->port.mode & MODE_TX) {
        if (uartPort->txDMAResource) {
            dma_init_type dmaInit;
            dma_default_para_init(&dmaInit);
            dmaInit.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
            dmaInit.loop_mode_enable = FALSE;
            dmaInit.peripheral_base_addr = uartPort->txDMAPeripheralBaseAddr;
            dmaInit.priority = DMA_PRIORITY_MEDIUM;
            dmaInit.peripheral_inc_enable = FALSE;
            dmaInit.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
            dmaInit.memory_inc_enable = TRUE;
            dmaInit.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
            dmaInit.buffer_size = uartPort->port.txBufferSize;

            xDMA_DeInit(uartPort->txDMAResource);
            xDMA_Init(uartPort->txDMAResource, &dmaInit);
            xDMA_ITConfig(uartPort->txDMAResource, DMA_IT_TCIF, ENABLE);
            xDMA_SetCurrDataCounter(uartPort->txDMAResource, 0);
            usart_dma_transmitter_enable(uartPort->USARTx, ENABLE);
        } else {
            usart_interrupt_enable(uartPort->USARTx, USART_TDBE_INT, ENABLE);
        }
    }

    usart_enable(uartPort->USARTx, ENABLE);
}

#ifdef USE_DMA
void uartTryStartTxDMA(uartPort_t *s)
{
    // uartTryStartTxDMA must be protected, since it is called from
    // uartWrite and handleUsartTxDma (an ISR).

    ATOMIC_BLOCK(NVIC_PRIO_SERIALUART_TXDMA) {
        if (IS_DMA_ENABLED(s->txDMAResource)) {
            // DMA is already in progress
            return;
        }

        if (xDMA_GetCurrDataCounter(s->txDMAResource)) {
            // Possible premature TC case.
            goto reenable;
        }

        if (s->port.txBufferHead == s->port.txBufferTail) {
            // No more data to transmit.
            s->txDMAEmpty = true;
            return;
        }

        // AT-BSP has no DMA_MemoryTargetConfig-style helper; poke the channel's
        // memory address register directly (matches betaflight's own AT32 UART port).
        ((DMA_ARCH_TYPE *)s->txDMAResource)->maddr = (uint32_t)&s->port.txBuffer[s->port.txBufferTail];

        if (s->port.txBufferHead > s->port.txBufferTail) {
            xDMA_SetCurrDataCounter(s->txDMAResource, s->port.txBufferHead - s->port.txBufferTail);
            s->port.txBufferTail = s->port.txBufferHead;
        } else {
            xDMA_SetCurrDataCounter(s->txDMAResource, s->port.txBufferSize - s->port.txBufferTail);
            s->port.txBufferTail = 0;
        }
        s->txDMAEmpty = false;

    reenable:
        xDMA_Cmd(s->txDMAResource, ENABLE);
    }
}

static void handleUsartTxDma(uartPort_t *s)
{
    uartTryStartTxDMA(s);
}

void uartDmaIrqHandler(dmaChannelDescriptor_t* descriptor)
{
    uartPort_t *s = &(((uartDevice_t*)(descriptor->userParam))->port);

    if (DMA_GET_FLAG_STATUS(descriptor, DMA_IT_TCIF)) {
        DMA_CLEAR_FLAG(descriptor, DMA_IT_TCIF);
        DMA_CLEAR_FLAG(descriptor, DMA_IT_HTIF);
        handleUsartTxDma(s);
    }
    if (DMA_GET_FLAG_STATUS(descriptor, DMA_IT_TEIF)) {
        DMA_CLEAR_FLAG(descriptor, DMA_IT_TEIF);
    }
}
#endif // USE_DMA

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

    uartPort_t *s = &(uart->port);
    s->port.vTable = uartVTable;

    s->port.baudRate = baudRate;

    s->port.rxBuffer = hardware->rxBuffer;
    s->port.txBuffer = hardware->txBuffer;
    s->port.rxBufferSize = hardware->rxBufferSize;
    s->port.txBufferSize = hardware->txBufferSize;

    s->USARTx = hardware->reg;

    if (hardware->rcc) {
        RCC_ClockCmd(hardware->rcc, ENABLE);
    }

#ifdef USE_DMA
    uartConfigureDma(uart);
#endif

    IO_t txIO = IOGetByTag(uart->tx.pin);
    IO_t rxIO = IOGetByTag(uart->rx.pin);

    if (options & SERIAL_BIDIR) {
        IOInit(txIO, OWNER_SERIAL_TX, RESOURCE_INDEX(device));
        IOConfigGPIOAF(txIO, ((options & SERIAL_BIDIR_PP) || (options & SERIAL_BIDIR_PP_PD)) ? IOCFG_AF_PP : IOCFG_AF_OD_UP, uart->tx.af);
    } else {
        if ((mode & MODE_TX) && txIO) {
            IOInit(txIO, OWNER_SERIAL_TX, RESOURCE_INDEX(device));
            IOConfigGPIOAF(txIO, IOCFG_AF_PP_UP, uart->tx.af);
        }

        if ((mode & MODE_RX) && rxIO) {
            IOInit(rxIO, OWNER_SERIAL_RX, RESOURCE_INDEX(device));
            IOConfigGPIOAF(rxIO, IOCFG_AF_PP_UP, uart->rx.af);
        }
    }

#ifdef USE_DMA
    if (!(s->rxDMAResource))
#endif
    {
        nvic_irq_enable(hardware->irqn, NVIC_PRIORITY_BASE(hardware->rxPriority), NVIC_PRIORITY_SUB(hardware->rxPriority));
    }

    return s;
}

void uartIrqHandler(uartPort_t *s)
{
    if (!s->rxDMAResource && usart_flag_get(s->USARTx, USART_RDBF_FLAG) == SET) {
        if (s->port.rxCallback) {
            s->port.rxCallback(UART_REG_RXD(s->USARTx), s->port.rxCallbackData);
        } else {
            s->port.rxBuffer[s->port.rxBufferHead] = UART_REG_RXD(s->USARTx);
            s->port.rxBufferHead = (s->port.rxBufferHead + 1) % s->port.rxBufferSize;
        }
    }

    if (!s->txDMAResource && usart_flag_get(s->USARTx, USART_TDBE_FLAG) == SET) {
        if (s->port.txBufferTail != s->port.txBufferHead) {
            usart_data_transmit(s->USARTx, s->port.txBuffer[s->port.txBufferTail]);
            s->port.txBufferTail = (s->port.txBufferTail + 1) % s->port.txBufferSize;
        } else {
            usart_interrupt_enable(s->USARTx, USART_TDBE_INT, DISABLE);
        }
    }

    if (usart_flag_get(s->USARTx, USART_ROERR_FLAG) == SET) {
        usart_flag_clear(s->USARTx, USART_ROERR_FLAG);
    }

    if (usart_flag_get(s->USARTx, USART_IDLEF_FLAG) == SET) {
        if (s->port.idleCallback) {
            s->port.idleCallback();
        }

        // clear IDLE by reading status then data register
        (void)s->USARTx->sts;
        (void)UART_REG_RXD(s->USARTx);
    }
}

#endif // USE_UART
