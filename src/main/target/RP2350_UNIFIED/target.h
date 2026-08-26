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

// Hardware UARTs only for the first RP2350/RP2354 bring-up (RP2350 has 2 real
// hardware UART peripherals: uart0/uart1, mapped here to wingflight's 1-based
// UARTDevice_e naming as UARTDEV_1/UARTDEV_2, matching STM32 targets'
// convention). PIO-based bit-banged "extra" UARTs (betaflight's
// USE_PIOUART0/1) are a follow-up, not yet implemented - see
// docs/RP2350-Porting-Plan.md.
#define USE_UART1
#define USE_UART2
#define SERIAL_PORT_COUNT 3 // USB VCP + 2 hardware UARTs

#define USE_SPI
#define SPIDEV_COUNT 2
#define USE_SPI_DEVICE_0
#define USE_SPI_DEVICE_1
#define USE_SPI_DMA_ENABLE_LATE

#define QUADSPIDEV_COUNT 1

#define USE_I2C
#define I2C_FULL_RECONFIGURABILITY
#define I2CDEV_COUNT 2
#define USE_I2C_DEVICE_0
#define USE_I2C_DEVICE_1

#define USE_ADC

// Generic, auto-detecting sensor/storage set - mirrors STM32_UNIFIED's
// target.h. Board-specific pin/bus assignment (GYRO_CS/GYRO_EXTI resources,
// gyro_1_bustype/gyro_1_spibus, baro_hardware, flash_spi_bus, etc.) happens
// entirely via the runtime CLI config, not compile-time target code - see
// wingflight-targets' example .config files. Compiling every known chip
// driver in and letting runtime detection pick the right one is the
// intended model, same as every STM32 unified target.
#define USE_ACC
#define USE_GYRO

#undef USE_ACC_MPU6050
#undef USE_GYRO_MPU6050
#undef USE_ACC_MPU6500
#undef USE_GYRO_MPU6500
#define USE_ACC_SPI_MPU6500
#define USE_GYRO_SPI_MPU6500
#define USE_ACC_SPI_MPU6000
#define USE_GYRO_SPI_MPU6000
#define USE_ACC_SPI_ICM20689
#define USE_GYRO_SPI_ICM20689
#undef USE_ACCGYRO_LSM6DSO
#undef USE_ACCGYRO_BMI160
#define USE_ACCGYRO_BMI270
#define USE_ACCGYRO_SPI_BMI323
#define USE_ACCGYRO_SPI_BMI088
#define USE_GYRO_SPI_ICM42605
#define USE_GYRO_SPI_ICM42688P
#define USE_ACC_SPI_ICM42605
#define USE_ACC_SPI_ICM42688P

#define USE_MAG
#define USE_MAG_DATA_READY_SIGNAL
#define USE_MAG_HMC5883
#define USE_MAG_SPI_HMC5883
#define USE_MAG_QMC5883
#define USE_MAG_LIS3MDL
#define USE_MAG_AK8963
#define USE_MAG_MPU925X_AK8963
#define USE_MAG_SPI_AK8963
#define USE_MAG_AK8975

#define USE_BARO
#define USE_BARO_MS5611
#define USE_BARO_SPI_MS5611
#define USE_BARO_BMP085
#define USE_BARO_BMP280
#define USE_BARO_SPI_BMP280
#define USE_BARO_BMP388
#define USE_BARO_SPI_BMP388
#define USE_BARO_LPS
#define USE_BARO_SPI_LPS
#define USE_BARO_QMP6988
#define USE_BARO_SPI_QMP6988
#define USE_BARO_DPS310
#define USE_BARO_SPI_DPS310
#define USE_BARO_BMP581
#define USE_BARO_SPI_BMP581

// Blackbox storage, decided 2026-08-26 (docs/RP2350-Porting-Plan.md): a
// dedicated external SPI flash chip and/or an SD card over SPI - not the
// on-die/QSPI firmware flash (see the porting plan for why). SDIO is
// STM32-hardware-specific and not applicable to PICO, so SPI-only for the
// SD card, matching the `SDCARD_SPI`/`ONBOARDFLASH` FEATURES in
// RP2350_UNIFIED/target.mk (not `SDCARD_SDIO`).
#define USE_SDCARD
#define USE_SDCARD_SPI

#define USE_FLASHFS
#define USE_FLASHFS_LOOP
#define USE_FLASH_TOOLS
#define USE_FLASH_M25P16
#define USE_FLASH_W25N01G
#define USE_FLASH_W25M
#define USE_FLASH_W25M512
#define USE_FLASH_W25M02G
#define USE_FLASH_W25Q128FV

#define USE_VCP

// USB-MSC over TinyUSB (drivers/usb_pico/usb_msc_pico.c implements the
// tud_msc_* callbacks against the SD-SPI / flashfs-emfat storage backends,
// wired via make/mcu/RP2350.mk's MSC_SRC). Must be defined here explicitly:
// common_pre.h only defines USE_USB_MSC inside its per-STM32-family blocks,
// which PICO never enters.
#define USE_USB_MSC

#define USE_SERIALRX_SBUS

#undef USE_SOFTSERIAL1
#undef USE_SOFTSERIAL2
#undef USE_TRANSPONDER
#undef USE_TIMER
#undef USE_RCC

// Assume on-board flash (see linker files)
#define CONFIG_IN_FLASH

// Pico flash writes are all aligned and in batches of pico-sdk's FLASH_PAGE_SIZE (256 bytes program granularity).
// Use a literal here (not pico-sdk's FLASH_PAGE_SIZE macro) since hardware/flash.h is not included at this point.
#define FLASH_CONFIG_STREAMER_BUFFER_SIZE   256
#define FLASH_CONFIG_BUFFER_TYPE            uint8_t

/* DMA Settings */
#define DMA_IRQ_CORE_NUM 1 // Use core 1 for DMA IRQs
#undef USE_DMA_SPEC // not yet required - possibly won't be used at all

#define USE_DSHOT
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

// Reserved for PIO-based extra UARTs - NOT implemented: serial is hardware
// uart0/uart1 only (serial_uart_pico.c). The earlier PIO-UART port was
// abandoned (upstream's ~1200-line uart/ subsystem is built on Betaflight's
// newer uartDevice_t serial architecture); see docs/RP2350-Porting-Plan.md
// for the deliberate deferral rationale.
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

// LED strip stays enabled (common_pre.h default): light_ws2811strip_pico.c
// was rewritten against wingflight's real contract - the shared
// one-word-per-bit ledStripDMABuffer stream is consumed directly by a PIO
// state machine shifting right with an autopull threshold of 1 (each DMA'd
// word yields its LSB as the wire bit, BIT_COMPARE_1=1/BIT_COMPARE_0=0), so
// the shared core loop needed no PICO branch at all.

// Various untested or unsupported elements are undefined below

#undef USE_RX_SPI
#undef USE_GYRO_REGISTER_DUMP
#undef USE_GPS_RESCUE
#undef USE_GPS_NAV
#undef USE_PPM
#undef USE_PWM
#undef USE_RX_PWM
#undef USE_RX_PPM
#undef USE_RX_CC2500
#undef USE_RX_EXPRESSLRS
#undef USE_RX_SX1280
#undef USE_SERIALRX_CRSF
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
#undef USE_TELEMETRY_CRSF
#undef USE_TELEMETRY_SBUS2
#undef USE_TELEMETRY_CASTLE
#undef USE_SPORT_MASTER
// USE_BLACKBOX stays enabled (common_pre.h default) now that a real storage
// backend is in scope - see the SDCARD/FLASH block above and
// docs/RP2350-Porting-Plan.md's blackbox storage decision.

// ESC 4-way / AM32/BLHeli forward-programming stays enabled (common_pre.h
// defaults + common_post.h's USE_SERIAL_4WAY_BLHELI_INTERFACE derivation):
// io/serial_4way*.c turned out to be written entirely against the generic
// IO API (IORead/IOHi/IOLo/IOConfigGPIO + micros() bit timing), all of
// which PICO now implements - only Bit_RESET (serial_4way.c) and motor-pin
// registration/function-restore (dshot_pico.c/pwm_motor_pico.c) needed
// PICO-specific handling.
#undef USE_SERIAL_PASSTHROUGH
#undef USE_MULTI_GYRO

#undef USE_RANGEFINDER_HCSR04
#undef USE_VTX_RTC6705
#undef USE_VTX_RTC6705_SOFTSPI
#undef USE_SRXL
#undef USE_SPEKTRUM
#undef USE_SPEKTRUM_BIND

#undef USE_CAMERA_CONTROL

#undef USE_RPM_LIMIT
