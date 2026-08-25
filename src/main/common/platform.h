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

#define NOINLINE __attribute__((noinline))

#if defined(STM32G474xx)
#include "stm32g4xx.h"
#include "stm32g4xx_hal.h"
#include "system_stm32g4xx.h"

#include "stm32g4xx_ll_spi.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_dma.h"
#include "stm32g4xx_ll_rcc.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_tim.h"
#include "stm32g4xx_ll_system.h"
#include "drivers/stm32g4xx_ll_ex.h"

// Chip Unique ID on G4
#define U_ID_0 (*(uint32_t*)UID_BASE)
#define U_ID_1 (*(uint32_t*)(UID_BASE + 4))
#define U_ID_2 (*(uint32_t*)(UID_BASE + 8))

#ifndef STM32G4
#define STM32G4
#endif

#elif defined(STM32H743xx) || defined(STM32H750xx) || defined(STM32H7A3xx) || defined(STM32H7A3xxQ) || defined(STM32H723xx) || defined(STM32H725xx) || defined(STM32H730xx)
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "system_stm32h7xx.h"

#include "stm32h7xx_ll_spi.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_rcc.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_tim.h"
#include "stm32h7xx_ll_system.h"
#include "drivers/stm32h7xx_ll_ex.h"

// Chip Unique ID on H7
#define U_ID_0 (*(uint32_t*)UID_BASE)
#define U_ID_1 (*(uint32_t*)(UID_BASE + 4))
#define U_ID_2 (*(uint32_t*)(UID_BASE + 8))

#ifndef STM32H7
#define STM32H7
#endif

#elif defined(STM32F722xx) || defined(STM32F745xx) || defined(STM32F746xx) || defined(STM32F765xx)
#include "stm32f7xx.h"
#include "stm32f7xx_hal.h"
#include "system_stm32f7xx.h"

#include "stm32f7xx_ll_spi.h"
#include "stm32f7xx_ll_gpio.h"
#include "stm32f7xx_ll_dma.h"
#include "stm32f7xx_ll_rcc.h"
#include "stm32f7xx_ll_bus.h"
#include "stm32f7xx_ll_tim.h"
#include "stm32f7xx_ll_system.h"
#include "drivers/stm32f7xx_ll_ex.h"

// Chip Unique ID on F7
#define U_ID_0 (*(uint32_t*)UID_BASE)
#define U_ID_1 (*(uint32_t*)(UID_BASE + 4))
#define U_ID_2 (*(uint32_t*)(UID_BASE + 8))

#ifndef STM32F7
#define STM32F7
#endif

#elif defined(STM32F40_41xxx) || defined (STM32F411xE) || defined (STM32F446xx)

#include "stm32f4xx.h"

// Chip Unique ID on F405
#ifndef UID_BASE
#define UID_BASE 0x1FFF7A10UL
#endif

#define U_ID_0 (*(uint32_t*)UID_BASE)
#define U_ID_1 (*(uint32_t*)(UID_BASE + 4))
#define U_ID_2 (*(uint32_t*)(UID_BASE + 8))

#ifndef STM32F4
#define STM32F4
#endif

#elif defined(PICO)

#include <stdint.h>

// RP2350/RP2354 (Raspberry Pi Pico 2 family). Individual pico-sdk headers
// (hardware/gpio.h, pico/stdlib.h, ...) are included directly by the *_pico.c
// driver files that need them, rather than centrally here.
//
// NOTE: deliberately NOT including the CMSIS device header (RP2350.h) here -
// it `#define`s the same peripheral base-address macros (SIO_BASE, PPB_BASE,
// ...) that pico-sdk's own hardware/regs/addressmap.h also defines, and the
// two conflict (redefinition errors) when both end up in the same
// translation unit. pico-sdk's own driver API (hardware/irq.h etc.) uses
// plain ints for IRQ numbers, not CMSIS's IRQn_Type enum, so it isn't needed.

// Wingflight's shared driver bookkeeping structs (io_impl.h's ioRec_t,
// bus_i2c_impl.h's i2cDevice_t, adc.h/dma.h) use STM32-style register-block
// pointer types and the CMSIS IRQn_Type enum purely for bookkeeping
// fields/signatures that the PICO drivers (io_pico.c, bus_i2c_pico.c, ...)
// never dereference - pico-sdk uses flat GPIO/peripheral-instance numbers and
// plain-int IRQ numbers instead. Declare these as opaque/placeholder types so
// the shared structs/declarations still compile unmodified for PICO.
typedef void GPIO_TypeDef;
typedef void I2C_TypeDef;
typedef void ADC_TypeDef;
typedef void DMA_TypeDef;
typedef void SPI_TypeDef;
// used by-value (not just by pointer) in drivers/bus.h, so it must be a
// complete type, unlike the opaque `void` placeholders above.
typedef struct DMA_InitTypeDef_s { uint32_t _unused_on_pico; } DMA_InitTypeDef;
typedef int32_t IRQn_Type;

// TODO: Chip Unique ID - use pico-sdk's pico_get_unique_board_id() instead of U_ID_x.

// Ported *_pico.c driver files store their pico-sdk peripheral pointers in
// the shared, platform-agnostic bookkeeping structs as opaque
// i2cResource_t*/spiResource_t*/DMA_TypeDef* fields, then cast back to the
// real pico-sdk instance type (i2c_inst_t*/spi_inst_t*/uart_inst_t*) via
// these macros before calling into pico-sdk. Mirrors betaflight's own
// PICO platform/platform.h macros of the same name.
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#define I2C_INST(i2c)  ((i2c_inst_t *)(i2c))
#define SPI_INST(spi)  ((spi_inst_t *)(spi))
#define UART_INST(uart) ((uart_inst_t *)(uart))

#elif defined(SIMULATOR_BUILD)

// Nop

#elif defined(UNIT_TEST)

#include "unittest_platform.h"

#else
#error "Invalid chipset specified. Update platform.h"
#endif

#if defined(UNIT_TEST)
#else
#include "target/common_pre.h"
#include "target.h"
#include "target/common_deprecated_post.h"
#include "target/common_post.h"
#include "target/common_defaults_post.h"
#endif
