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

#elif defined(AT32F43x)
// NOTE: dispatch on our own AT32F43x family macro (set via DEVICE_FLAGS in make/mcu/AT32F4.mk),
// not the vendor's AT32F435xx -- that macro is only DEFINED internally by at32f435_437.h itself
// (derived from the specific part macro, e.g. AT32F435RGT7) and isn't available yet at this
// point in the preprocessor pass, before the header below has even been included.
#include "at32f435_437.h"

// AT-BSP uses its own confirm_state/TRUE/FALSE instead of FunctionalState/ENABLE/DISABLE;
// provide the StdPeriph-style names since Wingflight's shared driver code (rcc.c, persistent.c,
// etc.) is written against them.
typedef enum {DISABLE = 0, ENABLE = !DISABLE} FunctionalState;

// AT-BSP's GPIO peripheral struct is named gpio_type; alias it to the StdPeriph-style name
// used throughout Wingflight's shared io.c/io_impl.h/etc.
#define GPIO_TypeDef gpio_type

// AT-BSP's DMA controller (not per-channel) struct is named dma_type; alias it to the
// StdPeriph-style name used by the shared dmaChannelDescriptor_t in drivers/dma.h.
#define DMA_TypeDef dma_type

// AT-BSP's timer peripheral struct is named tmr_type; alias it to the StdPeriph-style
// name used by shared code (dma_reqmap.c, timer.h/timer_def.h, etc.).
#define TIM_TypeDef tmr_type

// AT-BSP's SPI peripheral struct is named spi_type; alias it to the StdPeriph-style name
// used by shared code (bus.h/bus_spi.h/bus_spi_impl.h).
#define SPI_TypeDef spi_type

// AT-BSP's DMA channel init struct is named dma_init_type; alias it to the StdPeriph-style
// name used by the shared busDevice_t/extDevice_t in bus.h.
#define DMA_InitTypeDef dma_init_type

// AT-BSP's I2C peripheral struct is named i2c_type; alias it to the StdPeriph-style name
// used by shared code (bus_i2c_impl.h).
#define I2C_TypeDef i2c_type

// AT-BSP's USART/UART peripheral struct is named usart_type; alias it to the StdPeriph-style
// name used by shared code (serial_uart.h/serial_uart_impl.h).
#define USART_TypeDef usart_type

// AT-BSP's output-compare config struct is named tmr_output_config_type; alias it to the
// StdPeriph-style name referenced by the shared timer.h public API (timerOCInit()).
// pwm_output.c now has its own "#elif defined(AT32F43x)" branch that builds this struct
// using native AT-BSP field names (oc_mode/oc_polarity/oc_output_state/etc, see
// tmr_output_config_type in at32f435_437_tmr.h) instead of the StdPeriph field names
// (TIM_OCMode/TIM_Pulse/etc) used by the STM32-only branch, so no separate _at32bsp.c
// replacement file is needed for that driver. light_ws2811strip_stdperiph.c is still
// pending AT32 support (see AT32F435_TODO.md); dshot_dpwm.h and dshot_bitbang.c needed no
// changes (fully MCU-generic already) and dshot_bitbang_stdperiph.c has its own
// dshot_bitbang_at32bsp.c counterpart.
#define TIM_OCInitTypeDef tmr_output_config_type

// AT-BSP's input-capture config struct is named tmr_input_config_type; alias it the same
// way for USE_DSHOT_TELEMETRY's bidirectional-DSHOT input-capture path (dshot_dpwm.h's
// motorDmaOutput_t.icInitStruct, pwm_output_dshot_at32bsp.c's pwmDshotSetDirectionInput()).
// Matches betaflight's own upstream AT32 platform.h alias of the same name.
#define TIM_ICInitTypeDef tmr_input_config_type

// AT-BSP's ADC peripheral struct is named adc_type; alias it to the StdPeriph-style name
// used by shared code (drivers/adc.h/adc_impl.h/adc.c).
#define ADC_TypeDef adc_type

// Chip Unique ID on AT32F435/437 (see AT32F435/437 reference manual, Electronic Signature section)
#ifndef UID_BASE
#define UID_BASE 0x1FFFF7E8UL
#endif
#define U_ID_0 (*(uint32_t*)UID_BASE)
#define U_ID_1 (*(uint32_t*)(UID_BASE + 4))
#define U_ID_2 (*(uint32_t*)(UID_BASE + 8))

// AT-BSP's own headers, unlike STM32's CMSIS device headers, don't provide these
// register-manipulation helpers; betaflight's own upstream AT32 platform.h defines the
// same three macros for the same reason. Needed by dshot_bitbang_at32bsp.c's direct
// GPIO cfgr/scr register manipulation.
#define WRITE_REG(REG, VAL)   ((REG) = (VAL))
#define READ_REG(REG)         ((REG))
#define MODIFY_REG(REG, CLEARMASK, SETMASK)  WRITE_REG((REG), (((READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))

#ifndef AT32F43x
#define AT32F43x
#endif

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
