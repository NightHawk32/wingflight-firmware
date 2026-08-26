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

#include "platform.h"

#include "drivers/io.h"
#include "drivers/io_impl.h"

#include "common/utils.h"
#include "hardware/gpio.h"

#if DEFIO_PORT_USED_COUNT > 1
#error PICO code currently based on a single io port
#endif

// RP2 GPIO hardware has no native open-drain output mode (unlike STM32's
// GPIO_OType_OD): emulate it by toggling direction. "High"/released is
// input (so an external or internal pull-up brings the line up, and a
// stretching peer holding it low is still readable via gpio_get()); "low"
// is output with the value pre-set to 0. See IO_CONFIG()/IOCFG_*_OD in
// drivers/io.h for the config-bit encoding.
static bool pinOpenDrain[DEFIO_PIN_USED_COUNT];

// Initialize all ioRec_t structures.
// PICO (single port) doesn't use the gpio field.
void IOInitGlobal(void)
{
    ioRec_t *ioRec = ioRecs;

    for (unsigned pin = 0; pin < DEFIO_PIN_USED_COUNT; pin++) {
        ioRec->pin = pin;
        ioRec++;
    }

#ifdef PICO_TRACE
#ifdef PICO_TRACE_TX_GPIO
    ioRecs[PICO_TRACE_TX_GPIO].owner = OWNER_SYSTEM;
#endif
#ifdef PICO_TRACE_RX_GPIO
    ioRecs[PICO_TRACE_RX_GPIO].owner = OWNER_SYSTEM;
#endif
#endif

    // Some boards (e.g. Hellbender) require a pin to be held low in order to generate a 5V / 9V
    // power supply from the main battery.
    // (TODO: should we manage a list of pins that we want to send low or high?)
#ifdef PICO_BEC_5V_ENABLE_PIN
    const int pin5 = IO_PINBYTAG(IO_TAG(PICO_BEC_5V_ENABLE_PIN));
    gpio_init(pin5);
    gpio_set_dir(pin5, 1);
#if PICO_BEC_ENABLE_NONINVERTED
    gpio_put(pin5, 1);
    bprintf("5V enable pin: %d set high", pin5);
#else
    gpio_put(pin5, 0);
    bprintf("5V enable pin: %d set low", pin5);
#endif
    ioRecs[pin5].owner = OWNER_SYSTEM;
#endif

#ifdef PICO_BEC_9V_ENABLE_PIN
    const int pin9 = IO_PINBYTAG(IO_TAG(PICO_BEC_9V_ENABLE_PIN));
    gpio_init(pin9);
    gpio_set_dir(pin9, 1);
#if PICO_BEC_ENABLE_NONINVERTED
    gpio_put(pin9, 1);
    bprintf("9V enable pin: %d set high", pin9);
#else
    gpio_put(pin9, 0);
    bprintf("9V enable pin: %d set low", pin9);
#endif
    ioRecs[pin9].owner = OWNER_SYSTEM;
#endif
}

uint32_t IO_EXTI_Line(IO_t io)
{
    UNUSED(io);
    return 0;
}

bool IORead(IO_t io)
{
    if (!io) {
        return false;
    }
    return gpio_get(IO_Pin(io));
}

// Drives ioPin low (output) or releases it (input, floats high via pull-up)
// depending on 'hi', for a pin previously configured as open-drain.
static void ioWriteOpenDrain(uint16_t ioPin, bool hi)
{
    if (hi) {
        gpio_set_dir(ioPin, GPIO_IN);
    } else {
        gpio_put(ioPin, 0);
        gpio_set_dir(ioPin, GPIO_OUT);
    }
}

void IOWrite(IO_t io, bool hi)
{
    if (!io) {
        return;
    }
    const uint16_t ioPin = IO_Pin(io);
    if (pinOpenDrain[ioPin]) {
        ioWriteOpenDrain(ioPin, hi);
    } else {
        gpio_put(ioPin, hi);
    }
}

void IOHi(IO_t io)
{
    if (!io) {
        return;
    }
    const uint16_t ioPin = IO_Pin(io);
    if (pinOpenDrain[ioPin]) {
        ioWriteOpenDrain(ioPin, true);
    } else {
        gpio_put(ioPin, 1);
    }
}

void IOLo(IO_t io)
{
    if (!io) {
        return;
    }
    const uint16_t ioPin = IO_Pin(io);
    if (pinOpenDrain[ioPin]) {
        ioWriteOpenDrain(ioPin, false);
    } else {
        gpio_put(ioPin, 0);
    }
}

void IOToggle(IO_t io)
{
    if (!io) {
        return;
    }
    IOWrite(io, !gpio_get(IO_Pin(io)));
}

void IOConfigGPIO(IO_t io, ioConfig_t cfg)
{
    /*
Alternate-function pin routing (UART/SPI/I2C/PWM peripherals) is not handled
here - the respective *_pico.c bus/peripheral drivers call gpio_set_function()
directly, so IOCFG_AF_* configs only ever apply the direction/pull/open-drain
bits, same as their non-AF counterparts (matches PICO's IOCFG_AF_* definitions
in drivers/io.h, which reuse the plain GPIO encoding).

SPI_IO_CS_CFG/SPI_IO_CS_HIGH_CFG (drivers/bus_spi.h) are STM32-only defines,
not used by PICO's SPI driver (bus_spi_pico.c manages CS pins directly).
IO_RESET_CFG is likewise STM32-only (used by NRST config, not applicable here).
    */
    if (!io) {
        return;
    }

    const uint16_t ioPin = IO_Pin(io);
    bprintf("pico IOConfigGPIO gpio %d for 0x%02x (0=in, 1=out)",ioPin, cfg);

    const gpio_function_t currentFunction = gpio_get_function(ioPin);
    if (currentFunction == GPIO_FUNC_NULL) {
        // Select GPIO_FUNC_SIO, set direction to input, clear output value (set to low)
        gpio_init(ioPin);
    } else if (currentFunction != GPIO_FUNC_SIO) {
        // IOConfigGPIO()'s contract is "make this a plain GPIO with the given
        // config" - callers legitimately reclaim a pin from a peripheral
        // function this way (e.g. bus_i2c_utils.c's i2cUnstick() bit-banging
        // recovery on a pin that was just running the I2C hardware block), so
        // force the switch rather than silently leaving the old function in
        // place (which would make gpio_put()/gpio_set_dir() below no-ops).
        bprintf("pico IOConfigGPIO: switching gpio %d function from %d to SIO", ioPin, currentFunction);
        gpio_set_function(ioPin, GPIO_FUNC_SIO);
    }

    const bool dir = cfg & 0x01;
    const bool pullUp = (cfg >> 1) & 0x01;
    const bool pullDown = (cfg >> 2) & 0x01;
    const bool openDrain = (cfg >> 3) & 0x01;

    gpio_set_pulls(ioPin, pullUp, pullDown);
    pinOpenDrain[ioPin] = openDrain;

    if (openDrain) {
        // Start released (input, floats/pulled high) rather than actively
        // driving low, matching the idle-high convention of the open-drain
        // buses (e.g. I2C) this is used for.
        gpio_put(ioPin, 0);
        gpio_set_dir(ioPin, GPIO_IN);
    } else {
        gpio_set_dir(ioPin, dir); // 0 = in, 1 = out
    }
}

IO_t IOGetByTag(ioTag_t tag)
{
    const int portIdx = DEFIO_TAG_GPIOID(tag);
    const int pinIdx = DEFIO_TAG_PIN(tag);

    if (portIdx < 0 || portIdx >= DEFIO_PORT_USED_COUNT) {
        return NULL;
    }

    if (pinIdx >= DEFIO_PIN_USED_COUNT) {
        return NULL;
    }

    return &ioRecs[pinIdx];
}

int IO_GPIOPortIdx(IO_t io)
{
    if (!io) {
        return -1;
    }
    return 0; // Single port
}

int IO_GPIO_PortSource(IO_t io)
{
    return IO_GPIOPortIdx(io);
}

// zero based pin index
int IO_GPIOPinIdx(IO_t io)
{
    if (!io) {
        return -1;
    }
    return IO_Pin(io);
}

int IO_GPIO_PinSource(IO_t io)
{
    return IO_GPIOPinIdx(io);
}
