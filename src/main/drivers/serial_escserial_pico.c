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
 * ESC serial passthrough for PICO - the RP2350 counterpart of
 * drivers/serial_escserial.c.
 *
 * The STM32 driver bit-bangs an 8N1 UART on the motor pin with two hardware
 * timers (one of them on the ESCSERIAL resource pin, used only as a bit
 * clock). Here the motor pin is handed to a PIO soft serial port instead
 * (openSoftSerialOnPin(), drivers/serial_softserial_pico.c), so no extra
 * pin or timer is needed and the ESCSERIAL resource is ignored:
 *
 *   BLHeli (bl)      single-wire half duplex on the motor pin, 19200 baud
 *   Castle           single-wire half duplex on the motor pin, 18880 baud
 *   KISS (ki)        transmit only on the motor pin, 38400 baud
 *   KISS all (cc)    transmit only on every motor pin, 38400 baud
 *
 * SimonK is not supported, as on STM32 (USE_ESCSERIAL_SIMONK is not
 * defined for any target).
 *
 * The ports use state machines of the soft serial PIO block; opening fails
 * if both SOFTSERIAL ports already use all of them.
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef USE_ESCSERIAL

#include "drivers/io.h"
#include "drivers/light_led.h"
#include "drivers/motor.h"
#include "drivers/serial.h"
#include "drivers/serial_escserial.h"
#include "drivers/serial_softserial.h"
#include "drivers/time.h"

#include "pg/motor.h"

enum {
    BAUDRATE_NORMAL = 19200,
    BAUDRATE_KISS   = 38400,
    BAUDRATE_CASTLE = 18880
};

#define ESCSERIAL_MAX_PORTS 2 // SOFTSERIAL_PIN_PORTS in serial_softserial_pico.c

static serialPort_t *escPorts[ESCSERIAL_MAX_PORTS];
static uint8_t escPortCount;

typedef enum {
    IDLE,
    HEADER_START,
    HEADER_M,
    HEADER_ARROW,
    HEADER_SIZE,
    HEADER_CMD,
    COMMAND_RECEIVED
} mspState_e;

typedef struct mspPort_s {
    uint8_t offset;
    uint8_t dataSize;
    uint8_t checksum;
    uint8_t indRX;
    uint8_t inBuf[10];
    mspState_e c_state;
    uint8_t cmdMSP;
} mspPort_t;

static mspPort_t currentPort;

// Same exit handshake as drivers/serial_escserial.c: MSP command 0xF4
// without payload ends the passthrough.
static bool processExitCommand(uint8_t c)
{
    if (currentPort.c_state == IDLE) {
        if (c == '$') {
            currentPort.c_state = HEADER_START;
        } else {
            return false;
        }
    } else if (currentPort.c_state == HEADER_START) {
        currentPort.c_state = (c == 'M') ? HEADER_M : IDLE;
    } else if (currentPort.c_state == HEADER_M) {
        currentPort.c_state = (c == '<') ? HEADER_ARROW : IDLE;
    } else if (currentPort.c_state == HEADER_ARROW) {
        if (c > 10) {
            currentPort.c_state = IDLE;
        } else {
            currentPort.dataSize = c;
            currentPort.offset = 0;
            currentPort.checksum = 0;
            currentPort.indRX = 0;
            currentPort.checksum ^= c;
            currentPort.c_state = HEADER_SIZE;
        }
    } else if (currentPort.c_state == HEADER_SIZE) {
        currentPort.cmdMSP = c;
        currentPort.checksum ^= c;
        currentPort.c_state = HEADER_CMD;
    } else if (currentPort.c_state == HEADER_CMD && currentPort.offset < currentPort.dataSize) {
        currentPort.checksum ^= c;
        currentPort.inBuf[currentPort.offset++] = c;
    } else if (currentPort.c_state == HEADER_CMD && currentPort.offset >= currentPort.dataSize) {
        if (currentPort.checksum == c) {
            currentPort.c_state = COMMAND_RECEIVED;

            if ((currentPort.cmdMSP == 0xF4) && (currentPort.dataSize == 0)) {
                currentPort.c_state = IDLE;
                return true;
            }
        } else {
            currentPort.c_state = IDLE;
        }
    }
    return false;
}

static void closeEscPorts(void)
{
    for (int i = 0; i < escPortCount; i++) {
        closeSoftSerial(escPorts[i]);
        escPorts[i] = NULL;
    }
    escPortCount = 0;
}

static bool openEscPort(ioTag_t tag, uint32_t baud, uint8_t mode)
{
    if (escPortCount >= ESCSERIAL_MAX_PORTS || !tag) {
        return false;
    }

    const bool txOnly = (mode == PROTOCOL_KISS || mode == PROTOCOL_KISSALL);
    serialPort_t *port = openSoftSerialOnPin(tag, baud,
        txOnly ? MODE_TX : MODE_RXTX,
        txOnly ? SERIAL_NOT_INVERTED : SERIAL_BIDIR);

    if (!port) {
        return false;
    }

    escPorts[escPortCount++] = port;
    return true;
}

bool escEnablePassthrough(serialPort_t *escPassthroughPort, const motorDevConfig_t *motorConfig, uint16_t escIndex, uint8_t mode)
{
    uint32_t escBaudrate;
    switch (mode) {
    case PROTOCOL_BLHELI:
        escBaudrate = BAUDRATE_NORMAL;
        break;
    case PROTOCOL_KISS:
    case PROTOCOL_KISSALL:
        escBaudrate = BAUDRATE_KISS;
        break;
    case PROTOCOL_CASTLE:
        escBaudrate = BAUDRATE_CASTLE;
        break;
    default:
        return false;
    }

    if ((mode == PROTOCOL_KISS) && (escIndex == 255)) {
        mode = PROTOCOL_KISSALL;
    } else if (escIndex >= MAX_SUPPORTED_MOTORS) {
        return false;
    }

    LED0_OFF;
    LED1_OFF;
    motorDisable();

    if (mode == PROTOCOL_KISSALL) {
        for (int i = 0; i < MAX_SUPPORTED_MOTORS; i++) {
            if (motorConfig->ioTags[i] != IO_TAG_NONE) {
                openEscPort(motorConfig->ioTags[i], escBaudrate, mode);
            }
        }
    } else {
        openEscPort(motorConfig->ioTags[escIndex], escBaudrate, mode);
    }

    if (!escPortCount) {
        return false;
    }

    // Give the ESC time to see the idle-high line, as on STM32
    delay(50);

    serialPort_t *escPort = escPorts[0];
    const bool receives = (mode != PROTOCOL_KISS && mode != PROTOCOL_KISSALL);

    while (1) {
        if (receives && serialRxBytesWaiting(escPort)) {
            LED0_ON;
            while (serialRxBytesWaiting(escPort)) {
                serialWrite(escPassthroughPort, serialRead(escPort));
            }
            LED0_OFF;
        }
        if (serialRxBytesWaiting(escPassthroughPort)) {
            LED1_ON;
            while (serialRxBytesWaiting(escPassthroughPort)) {
                const uint8_t ch = serialRead(escPassthroughPort);
                if (processExitCommand(ch)) {
                    serialWrite(escPassthroughPort, 0x24);
                    serialWrite(escPassthroughPort, 0x4D);
                    serialWrite(escPassthroughPort, 0x3E);
                    serialWrite(escPassthroughPort, 0x00);
                    serialWrite(escPassthroughPort, 0xF4);
                    serialWrite(escPassthroughPort, 0xF4);
                    closeEscPorts();
                    return true;
                }
                if (mode == PROTOCOL_BLHELI) {
                    serialWrite(escPassthroughPort, ch); // blheli loopback
                }
                for (int i = 0; i < escPortCount; i++) {
                    serialWrite(escPorts[i], ch);
                }
            }
            LED1_OFF;
        }
        if (mode != PROTOCOL_CASTLE) {
            delay(5);
        }
    }
}

#endif
