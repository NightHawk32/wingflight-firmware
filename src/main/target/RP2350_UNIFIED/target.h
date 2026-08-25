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

#pragma once

/*
 * Common definitions for all RP2350/RP2354 targets.
 * Ported from betaflight/src/platform/PICO/target/common/target_RP2350.h
 * and betaflight/src/platform/PICO/target/{RP2350A,RP2350B}/target.h,
 * merged into wingflight's unified-target layout (one target.mk dispatches
 * on TARGET=RP2350A/RP2350B/RP2354A/RP2354B, see RP2350_UNIFIED/target.mk).
 */

#define USE_UNIFIED_TARGET

#if defined(RP2350A)
#define TARGET_BOARD_IDENTIFIER "235A"
#define USBD_PRODUCT_STRING     "Wingflight - RP2350A"
#define UARTHARDWARE_MAX_PINS 8
#define MAX_SPI_PIN_SEL 4
#elif defined(RP2350B)
#define TARGET_BOARD_IDENTIFIER "235B"
#define USBD_PRODUCT_STRING     "Wingflight - RP2350B"
#define UARTHARDWARE_MAX_PINS 12
#define MAX_SPI_PIN_SEL 6
#elif defined(RP2354A)
// Derived from RP2350A: same package/pinout, on-die 2MB flash instead of external QSPI.
#define TARGET_BOARD_IDENTIFIER "254A"
#define USBD_PRODUCT_STRING     "Wingflight - RP2354A"
#define UARTHARDWARE_MAX_PINS 8
#define MAX_SPI_PIN_SEL 4
#elif defined(RP2354B)
// Derived from RP2350B: same package/pinout, on-die 2MB flash instead of external QSPI.
#define TARGET_BOARD_IDENTIFIER "254B"
#define USBD_PRODUCT_STRING     "Wingflight - RP2354B"
#define UARTHARDWARE_MAX_PINS 12
#define MAX_SPI_PIN_SEL 6
#else
#error "No RP2350/RP2354 variant defined. TARGET must be one of RP2350A, RP2350B, RP2354A, RP2354B."
#endif

// USE_MULTICORE turns on the multicore API (core 1 + dispatch).
// ENABLE_MULTICORE_INIT additionally runs the FC init phases on core 1 (the
// RP2350 core-allocation policy); enable both for testing multicore.
//#define USE_MULTICORE
//#define ENABLE_MULTICORE_INIT

#define USE_UART0
#define USE_UART1
#define USE_PIOUART0
#define USE_PIOUART1
#define UART_TRAIT_AF_PORT 1

#define USE_SPI
#define SPIDEV_COUNT 2
#define USE_SPI_DEVICE_0
#define USE_SPI_DEVICE_1
#define USE_SPI_DMA_ENABLE_LATE

#define QUADSPIDEV_COUNT 1

#define USE_I2C
#define I2CDEV_COUNT 2
#define USE_I2C_DEVICE_0
#define USE_I2C_DEVICE_1

#define USE_ADC

#define USE_VCP

#define USE_USB_MSC

#define USE_SERIALRX_SBUS

#undef USE_SOFTSERIAL1
#undef USE_SOFTSERIAL2
#undef USE_TRANSPONDER
#undef USE_TIMER
#undef USE_RCC

// Assume on-board flash (see linker files)
#define CONFIG_IN_FLASH

// Pico flash writes are all aligned and in batches of FLASH_PAGE_SIZE (256)
#define FLASH_CONFIG_STREAMER_BUFFER_SIZE   FLASH_PAGE_SIZE
#define FLASH_CONFIG_BUFFER_TYPE            uint8_t

/* DMA Settings */
#define DMA_IRQ_CORE_NUM 1 // Use core 1 for DMA IRQs
#undef USE_DMA_SPEC // not yet required - possibly won't be used at all

#undef USE_DSHOT_BITBANG
#define USE_DSHOT_TELEMETRY

// 0, 1 or 2 for pio0, pio1, pio2
// These can be predefined in config.h
// Four state machines (sm) per pio block
// Defaults
// pio0 -> dshot for motors 1,2,3,4
// pio1 -> PIOUART0, PIOUART1
// pio2 -> LED STRIP
#ifndef PIO_DSHOT_INDEX
#define PIO_DSHOT_INDEX    0
#endif

#ifndef PIO_UART_INDEX
#define PIO_UART_INDEX     1
#endif

#ifndef PIO_LEDSTRIP_INDEX
#define PIO_LEDSTRIP_INDEX 2
#endif

#ifdef REMOVE_MSP_DISPLAYPORT
#undef USE_MSP_DISPLAYPORT
#endif

// OSD framebuffer support (betaflight PICO/osd/*) intentionally not ported - dropped per project scope.
#undef USE_OSD
#undef USE_FRSKYOSD
#undef USE_MSP_DISPLAYPORT

// Various untested or unsupported elements are undefined below

#undef USE_RX_SPI
#undef USE_RX_PWM
#undef USE_RX_PPM
#undef USE_RX_CC2500
#undef USE_RX_EXPRESSLRS
#undef USE_RX_SX1280
#undef USE_SERIALRX_GHST
#undef USE_SERIALRX_IBUS
#undef USE_SERIALRX_JETIEXBUS
#undef USE_SERIALRX_SPEKTRUM
#undef USE_SERIALRX_SUMD
#undef USE_SERIALRX_SUMH
#undef USE_SERIALRX_XBUS
#undef USE_SERIALRX_FPORT

#undef USE_TELEMETRY_GHST
#undef USE_TELEMETRY_FRSKY_HUB
#undef USE_TELEMETRY_HOTT
#undef USE_TELEMETRY_IBUS
#undef USE_TELEMETRY_IBUS_EXTENDED
#undef USE_TELEMETRY_JETIEXBUS
#undef USE_TELEMETRY_LTM
#undef USE_TELEMETRY_MAVLINK
#undef USE_TELEMETRY_SMARTPORT
#undef USE_TELEMETRY_SRXL

#undef USE_SERIAL_4WAY_BLHELI_INTERFACE
#undef USE_SERIAL_4WAY_BLHELI_BOOTLOADER
#undef USE_SERIAL_4WAY_SK_BOOTLOADER
#undef USE_MULTI_GYRO

#undef USE_RANGEFINDER_HCSR04
#undef USE_VTX_RTC6705
#undef USE_VTX_RTC6705_SOFTSPI
#undef USE_SRXL
#undef USE_SPEKTRUM
#undef USE_SPEKTRUM_BIND

#undef USE_SERIAL_PASSTHROUGH

#undef USE_RPM_LIMIT
