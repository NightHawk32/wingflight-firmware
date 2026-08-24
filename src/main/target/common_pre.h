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

#define USE_PARAMETER_GROUPS
// type conversion warnings.
// -Wconversion can be turned on to enable the process of eliminating these warnings
//#pragma GCC diagnostic warning "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
// -Wpadded can be turned on to check padding of structs
//#pragma GCC diagnostic warning "-Wpadded"

#ifdef STM32F4
#if defined(STM32F40_41xxx)
#define USE_FAST_DATA
#endif
#define USE_DSHOT
#define USE_DSHOT_BITBANG
#define USE_DSHOT_TELEMETRY
#define USE_DSHOT_TELEMETRY_STATS
#define USE_RPM_FILTER
#define USE_DYN_NOTCH_FILTER
#define USE_ADC_INTERNAL
#define USE_USB_CDC_HID
#define USE_USB_MSC
#define USE_PERSISTENT_MSC_RTC
#define USE_MCO
#define USE_DMA_SPEC
#define USE_TIMER_MGMT
#define USE_PERSISTENT_OBJECTS
#define USE_CUSTOM_DEFAULTS_ADDRESS
#define USE_LATE_TASK_STATISTICS

#if defined(STM32F40_41xxx) || defined(STM32F411xE)
#define USE_OVERCLOCK
#endif
#endif // STM32F4

#ifdef STM32F7
#define USE_ITCM_RAM
#define ITCM_RAM_OPTIMISATION "-O2", "-freorder-blocks-algorithm=simple"
#define USE_FAST_DATA
#define USE_DSHOT
#define USE_DSHOT_BITBANG
#define USE_DSHOT_TELEMETRY
#define USE_DSHOT_TELEMETRY_STATS
#define USE_RPM_FILTER
#define USE_DYN_NOTCH_FILTER
#define USE_OVERCLOCK
#define USE_ADC_INTERNAL
#define USE_USB_CDC_HID
#define USE_USB_MSC
#define USE_PERSISTENT_MSC_RTC
#define USE_MCO
#define USE_DMA_SPEC
#define USE_TIMER_MGMT
#define USE_PERSISTENT_OBJECTS
#define USE_CUSTOM_DEFAULTS_ADDRESS
#define USE_LATE_TASK_STATISTICS
#define USE_TELEMETRY_SBUS2
#define USE_TELEMETRY_CASTLE
#endif // STM32F7

#ifdef STM32H7
#define USE_ITCM_RAM
#define USE_FAST_DATA
#define USE_DSHOT
#define USE_DSHOT_BITBANG
#define USE_DSHOT_TELEMETRY
#define USE_DSHOT_TELEMETRY_STATS
#define USE_RPM_FILTER
#define USE_DYN_NOTCH_FILTER
#define USE_OVERCLOCK
#define USE_ADC_INTERNAL
#define USE_USB_CDC_HID
#define USE_DMA_SPEC
#define USE_TIMER_MGMT
#define USE_PERSISTENT_OBJECTS
#define USE_DMA_RAM
#define USE_USB_MSC
#define USE_RTC_TIME
#define USE_PERSISTENT_MSC_RTC
#define USE_DSHOT_CACHE_MGMT
#define USE_LATE_TASK_STATISTICS
#define USE_TELEMETRY_SBUS2
#define USE_TELEMETRY_CASTLE
#endif

#ifdef STM32G4
#define USE_DSHOT
#define USE_DSHOT_BITBANG
#define USE_DSHOT_TELEMETRY
#define USE_DSHOT_TELEMETRY_STATS
#define USE_RPM_FILTER
#define USE_OVERCLOCK
#define USE_DYN_NOTCH_FILTER
#define USE_ADC_INTERNAL
#define USE_USB_MSC
#define USE_PERSISTENT_MSC_RTC
#define USE_USB_CDC_HID
#define USE_MCO
#define USE_DMA_SPEC
#define USE_TIMER_MGMT
#define USE_PERSISTENT_OBJECTS
#define USE_LATE_TASK_STATISTICS
#define USE_TELEMETRY_CASTLE
#endif

#ifdef AT32F43x
// Minimal set for now — only what's actually needed by driver code already written
// (dma_reqmap.c's AT32 branch is entirely guarded by USE_DMA_SPEC, so this must be
// defined for that prior work to compile in at all). USE_TIMER_MGMT is now safe to add
// too since drivers/timer_at32bsp.c and timer_at32f43x.c's fullTimerHardware[] table
// exist and this is a unified build (needs the dynamic timerHardware/CLI `timer` command
// path, not a fixed per-board timerHardware[] table). USE_USB_MSC is now enabled too
// since drivers/usb_msc_at32f43x.c/drivers/msc_desc.h/drivers/msc_diskio.h exist (see
// AT32F435_TODO.md). Add USE_USB_CDC_HID/USE_MCO/etc. once needed.
//
// USE_DSHOT is now enabled: drivers/pwm_output_dshot_at32bsp.c implements classic
// single-channel DMA-to-CCR DSHOT output natively against AT-BSP. Deliberately NOT yet
// enabled alongside it:
//   - USE_DSHOT_DMAR (4-channel timer-update-event burst DMA into consecutive CCR
//     registers via STM32's ->DMAR alias register) -- AT-BSP timers have no equivalent
//     alias register, so this needs its own implementation strategy later.
//   - USE_DSHOT_TELEMETRY / USE_DSHOT_TELEMETRY_STATS (bidirectional DSHOT / ESC
//     telemetry, which periodically switches the timer channel between output-compare
//     and input-capture) -- not yet implemented in pwm_output_dshot_at32bsp.c.
//   - USE_DSHOT_BITBANG -- drivers/dshot_bitbang_stdperiph.c (the StdPeriph DMA-GPIO
//     bitbang backend) is STM32-only StdPeriph code with no AT-BSP port yet; would need
//     its own _at32bsp.c equivalent (see AT32F435_TODO.md). dshot_bitbang.c/
//     dshot_bitbang_decode.c stay listed in MCU_COMMON_SRC as harmless empty translation
//     units (entirely guarded by #ifdef USE_DSHOT_BITBANG) until then.
//
// USE_ADC_INTERNAL is now enabled: drivers/adc_at32f43x.c implements vrefint/tempsensor
// as an ADC1 preempt (injected) channel pair against AT-BSP's preempt-channel API, using
// fixed nominal datasheet calibration constants (AT32 has no per-chip OTP calibration
// words, unlike STM32) sourced from betaflight's own official AT32 ADC driver.
#define USE_DMA_SPEC
#define USE_TIMER_MGMT
#define USE_DSHOT
#define USE_USB_MSC
#define USE_PERSISTENT_MSC_RTC
#define USE_ADC_INTERNAL
#endif

#if defined(STM32F4) || defined(STM32F7) || defined(STM32H7) || defined(STM32G4) || defined(AT32F43x)
#define TASK_GYROPID_DESIRED_PERIOD     125 // 125us = 8kHz
#define SCHEDULER_DELAY_LIMIT           10
#else
#define TASK_GYROPID_DESIRED_PERIOD     1000 // 1000us = 1kHz
#define SCHEDULER_DELAY_LIMIT           100
#endif

// Set the default cpu_overclock to the first level (108MHz) for F411
// Helps with looptime stability as the CPU is borderline when running native gyro sampling
#if defined(USE_OVERCLOCK) && defined(STM32F411xE)
#define DEFAULT_CPU_OVERCLOCK 1
#else
#define DEFAULT_CPU_OVERCLOCK 0
#endif

#if defined(STM32H7) || defined(STM32F7)
// Move ISRs to fast ram to avoid flash latency.
#define FAST_IRQ_HANDLER FAST_CODE
#else
#define FAST_IRQ_HANDLER
#endif

#define INIT_CODE                   __attribute__((optimize("-Os")))

#ifdef USE_ITCM_RAM
#if defined(ITCM_RAM_OPTIMISATION) && !defined(DEBUG_BUILD)
#define FAST_CODE                   __attribute__((section(".tcm_code"))) __attribute__((optimize(ITCM_RAM_OPTIMISATION)))
#else
#define FAST_CODE                   __attribute__((section(".tcm_code")))
#endif
// Handle case where we'd prefer code to be in ITCM, but it won't fit on the F745
#ifdef STM32F745xx
#define FAST_CODE_PREF
#else
#define FAST_CODE_PREF                  __attribute__((section(".tcm_code")))
#endif
#define FAST_CODE_NOINLINE          NOINLINE
#else
#define FAST_CODE
#define FAST_CODE_PREF
#define FAST_CODE_NOINLINE
#endif // USE_ITCM_RAM

#ifdef USE_CCM_CODE
#define CCM_CODE                    __attribute__((section(".ccm_code")))
#else
#define CCM_CODE
#endif

#ifdef USE_FAST_DATA
#define FAST_DATA_ZERO_INIT         __attribute__ ((section(".fastram_bss"), aligned(4)))
#define FAST_DATA                   __attribute__ ((section(".fastram_data"), aligned(4)))
#else
#define FAST_DATA_ZERO_INIT
#define FAST_DATA
#endif // USE_FAST_DATA

#if defined(STM32F4) || defined(STM32G4) || defined(AT32F43x)
// F4 can't DMA to/from CCM (core coupled memory) SRAM (where the stack lives)
// On G4 there is no specific DMA target memory
// AT32F435/437 (AT-BSP) also has no known DMA-target-memory restriction, and no dedicated
// DMA-RAM linker section exists for it yet -- matches the same "no special region needed"
// choice already made for AT32's UART_TX_BUFFER_ATTRIBUTE/UART_RX_BUFFER_ATTRIBUTE.
#define DMA_DATA_ZERO_INIT
#define DMA_DATA
#define STATIC_DMA_DATA_AUTO        static
#elif defined (STM32F7)
// F7 has no cache coherency issues DMAing to/from DTCM, otherwise buffers must be cache aligned
#define DMA_DATA_ZERO_INIT          FAST_DATA_ZERO_INIT
#define DMA_DATA                    FAST_DATA
#define STATIC_DMA_DATA_AUTO        static DMA_DATA
#else
// DMA to/from any memory
#define DMA_DATA_ZERO_INIT          __attribute__ ((section(".dmaram_bss"), aligned(32)))
#define DMA_DATA                    __attribute__ ((section(".dmaram_data"), aligned(32)))
#define STATIC_DMA_DATA_AUTO        static DMA_DATA
#endif

#if defined(STM32F4) || defined (STM32H7)
// Data in RAM which is guaranteed to not be reset on hot reboot
#define PERSISTENT                  __attribute__ ((section(".persistent_data"), aligned(4)))
#endif

#ifdef USE_DMA_RAM
#if defined(STM32H7)
#define DMA_RAM __attribute__((section(".DMA_RAM"), aligned(32)))
#define DMA_RW_AXI __attribute__((section(".DMA_RW_AXI"), aligned(32)))
extern uint8_t _dmaram_start__;
extern uint8_t _dmaram_end__;
#elif defined(STM32G4)
#define DMA_RAM_R __attribute__((section(".DMA_RAM_R")))
#define DMA_RAM_W __attribute__((section(".DMA_RAM_W")))
#define DMA_RAM_RW __attribute__((section(".DMA_RAM_RW")))
#endif
#else
#define DMA_RAM
#define DMA_RW_AXI
#define DMA_RAM_R
#define DMA_RAM_W
#define DMA_RAM_RW
#endif

#define USE_MOTOR
#define USE_PWM_OUTPUT
#define USE_DMA
#define USE_TIMER

#ifndef STM32F4
#define USE_SERIAL_PINSWAP
#endif

#define USE_CLI
#define USE_SERIAL_PASSTHROUGH
#define USE_GYRO_REGISTER_DUMP  // Adds gyroregisters command to cli to dump configured register values
#define USE_IMU_CALC
#define USE_PPM

// ---- Wingflight fork identifier ------------------------------------------
// Activates WINGFLIGHT_AIRPLANE code paths in all builds of this firmware fork.
// See src/main/fc/runtime_config.h for the companion documentation comment.
#define WINGFLIGHT_AIRPLANE
// --------------------------------------------------------------------------
#define USE_SERIAL_RX
#define USE_SERIALRX_CRSF       // Team Black Sheep Crossfire protocol
#define USE_SERIALRX_GHST       // ImmersionRC Ghost Protocol
#define USE_SERIALRX_IBUS       // FlySky and Turnigy receivers
#define USE_SERIALRX_IBUS2      // FlySky AFHDS3 receivers
#define USE_SERIALRX_SBUS       // Frsky and Futaba receivers
#define USE_SERIALRX_SPEKTRUM   // SRXL, DSM2 and DSMX protocol
#define USE_SERIALRX_SUMD       // Graupner Hott protocol
#define USE_SBUS_OUTPUT         // SBus Output feature
#define USE_FBUS_MASTER         // FBUS Master feature
#define USE_SPORT_MASTER        // S.Port master feature
#if defined(USE_SBUS_OUTPUT) || defined(USE_FBUS_MASTER)
#define USE_BUS_SERVO
#endif

#if (TARGET_FLASH_SIZE > 256)
#define PID_PROFILE_COUNT 6
#define CONTROL_RATE_PROFILE_COUNT  6
#elif (TARGET_FLASH_SIZE > 128)
#define PID_PROFILE_COUNT 3
#define CONTROL_RATE_PROFILE_COUNT  6
#else
#define PID_PROFILE_COUNT 2
#define CONTROL_RATE_PROFILE_COUNT  3
#endif

#if (TARGET_FLASH_SIZE > 64)
#define USE_ACRO_TRAINER
#define USE_BLACKBOX
#define USE_CLI_BATCH
#define USE_RESOURCE_MGMT
#define USE_SERVOS
#define USE_TELEMETRY
#define USE_TELEMETRY_FRSKY_HUB
#define USE_TELEMETRY_SMARTPORT
#endif

#if (TARGET_FLASH_SIZE > 128)
#define USE_GYRO_OVERFLOW_CHECK
#define USE_DSHOT_DMAR
#define USE_SERIALRX_FPORT      // FrSky FPort
#define USE_SERIALRX_FBUS       // FrSky FBUS/FPORT2
#define USE_SMARTFUEL
#define USE_TELEMETRY_CRSF
#define USE_TELEMETRY_GHST
#define USE_TELEMETRY_SRXL

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 12))
#define USE_MSP_OVER_TELEMETRY
#define USE_LED_STRIP
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 11))
#define USE_VTX_COMMON
#define USE_VTX_CONTROL
#define USE_VTX_SMARTAUDIO
#define USE_VTX_TRAMP
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 10))
#define USE_CAMERA_CONTROL
#define USE_ESC_SENSOR
#define USE_SERIAL_4WAY_BLHELI_BOOTLOADER
#define USE_RCDEVICE
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 9))
#define USE_GYRO_LPF2
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 8))
#define USE_DYN_LPF
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 7))
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 6))
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 5))
#define USE_PWM
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 4))
#define USE_HUFFMAN
#define USE_PINIO
#define USE_PINIOBOX
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 3))
#ifdef USE_SERIALRX_SPEKTRUM
#define USE_SPEKTRUM_BIND
#define USE_SPEKTRUM_BIND_PLUG
#define USE_SPEKTRUM_REAL_RSSI
#define USE_SPEKTRUM_FAKE_RSSI
#define USE_SPEKTRUM_RSSI_PERCENT_CONVERSION
#define USE_SPEKTRUM_VTX_CONTROL
#define USE_SPEKTRUM_VTX_TELEMETRY
#define USE_PIN_PULL_UP_DOWN
#endif
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 2))
#define USE_TELEMETRY_HOTT
#define USE_TELEMETRY_LTM
#define USE_SERIALRX_SUMH       // Graupner legacy protocol
#define USE_SERIALRX_XBUS       // JR
#endif

#if ((TARGET_FLASH_SIZE > 256) || (FEATURE_CUT_LEVEL < 1))
#define USE_BOARD_INFO
#define USE_RTC_TIME
#define USE_RX_MSP
#define USE_CRSF_LINK_STATISTICS
#define USE_RX_RSSI_DBM
#endif

#endif // TARGET_FLASH_SIZE > 128

#if (TARGET_FLASH_SIZE > 256)
#define USE_GPS
#define USE_GPS_NMEA
#define USE_GPS_UBLOX
#define USE_GPS_RESCUE
#define USE_GPS_NAV
#define USE_GYRO_DLPF_EXPERIMENTAL
#define USE_MULTI_GYRO
#define USE_SENSOR_NAMES
#define USE_SERIALRX_JETIEXBUS
#define USE_TELEMETRY_IBUS
#define USE_TELEMETRY_IBUS2
#define USE_TELEMETRY_IBUS_EXTENDED
#define USE_TELEMETRY_JETIEXBUS
#define USE_TELEMETRY_MAVLINK
#define USE_SIGNATURE
#define USE_LED_STRIP_STATUS_MODE
#define USE_VARIO
#define USE_ESC_SENSOR_TELEMETRY
#define USE_VTX_TABLE
#define USE_PERSISTENT_STATS
#define USE_PROFILE_NAMES
#define USE_SERIALRX_SRXL2     // Spektrum SRXL2 protocol
#define USE_CUSTOM_BOX_NAMES
#define USE_CRSF_V3
#define USE_SERIAL_4WAY_BLHELI_BOOTLOADER
#define USE_SERIAL_4WAY_SK_BOOTLOADER
#define USE_BLHELI_FORWARD_PROGRAMMING
#define USE_AM32_FORWARD_PROGRAMMING
#endif

#if (TARGET_FLASH_SIZE > 512)
#define USE_ESCSERIAL_SIMONK
#define USE_SERIAL_4WAY_SK_BOOTLOADER
#define USE_DASHBOARD
#define USE_EMFAT_AUTORUN
#define USE_EMFAT_ICON
#define USE_GPS_PLUS_CODES
#endif

#if defined(STM32G4)
#define MAX_PID_PROCESS_SPEED       1600
#elif defined(STM32F411xE)
#define MAX_PID_PROCESS_SPEED       1600
#elif defined(STM32F4)
#define MAX_PID_PROCESS_SPEED       4000
#elif defined(STM32F7)
#define MAX_PID_PROCESS_SPEED       4000
#elif defined(STM32H7)
#define MAX_PID_PROCESS_SPEED       8000
#else
#define MAX_PID_PROCESS_SPEED       1000
#endif

#define MIN_PID_PROCESS_SPEED       800
