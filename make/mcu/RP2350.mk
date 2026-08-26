#
# RP2350/RP2354 (Raspberry Pi Pico 2 family) Make file include
#
# Ported from betaflight/src/platform/PICO/mk/RP2350.mk, adapted to
# wingflight's flat (non platform-abstracted) build layout:
#   - TARGET_PLATFORM_DIR does not exist here; PICO-specific driver sources
#     live directly under $(SRC_DIR)/drivers (suffixed *_pico.c, matching the
#     convention used by other MCU families, e.g. adc_stm32h7xx.c).
#   - OSD (PICO/osd/*) is intentionally NOT ported - dropped per project scope.
#

ifeq ($(DEBUG_HARDFAULTS),RP2350)
CFLAGS          += -DDEBUG_HARDFAULTS
endif

# pico-sdk headers (e.g. hardware/dma.h's invalid_params_if()/valid_params_if()
# macros, and pico/platform.h's busy_wait_at_least_cycles() which embeds
# #ifdef directives inside a macro argument list) use GNU/pedantic-flagged
# constructs that -Wpedantic (enabled globally in the root Makefile's CFLAGS)
# turns into hard errors under -Werror. Betaflight's own build does not
# enable -Wpedantic at all, so this never came up upstream. Note:
# TEMPORARY_FLAGS is unconditionally reset with `:=` further down in the root
# Makefile (after this file is included), so it can't be used here - use
# EXTRA_FLAGS instead, which is appended last in CFLAGS and never reset.
# Disabled only for RP2350/RP2354 builds - STM32/other targets are unaffected.
EXTRA_FLAGS += -Wno-pedantic

# Run from SRAM. To disable, set environment variable RUN_FROM_RAM=0
ifeq ($(RUN_FROM_RAM),)
RUN_FROM_RAM = 1
endif

# UF2 output (for BOOTSEL drag-and-drop / picotool flashing). Family ID is
# RP2350_ARM_S_FAMILY_ID from pico-sdk's boot/uf2.h (secure ARM image); base
# address matches FLASH_ORIGIN in src/link/pico_rp2350_memory.ld, which is
# also where the flat .bin's lowest LMA starts.
UF2_FAMILY_ID   := 0xe48bff59
UF2_BASE_ADDR   := 0x10000000

PICO_LIB_OPTIMISATION := -O2 -fuse-linker-plugin -ffast-math -fmerge-all-constants

SDK_DIR         = $(LIB_MODULES_DIR)/pico-sdk/src

#CMSIS (pico-sdk's own CMSIS stub, distinct from lib/main/CMSIS used by STM32 targets)
CMSIS_DIR       := $(SDK_DIR)/rp2_common/cmsis/stub/CMSIS

#STDPERIPH (unused for PICO - pico-sdk sources are listed explicitly in PICO_LIB_SRC below)
STDPERIPH_DIR   := $(SDK_DIR)
STDPERIPH_SRC   :=

VPATH           := $(VPATH):$(STDPERIPH_DIR)

TARGET_MCU_LIB_LOWER = rp2350
TARGET_MCU_LIB_UPPER = RP2350

#CMSIS
VPATH       := $(VPATH):$(CMSIS_DIR)/Core/Include:$(CMSIS_DIR)/Device/$(TARGET_MCU_LIB_UPPER)/Include
CMSIS_SRC   :=

PICO_LIB_SRC = \
            rp2_common/pico_crt0/crt0.S \
            rp2_common/hardware_sync_spin_lock/sync_spin_lock.c \
            rp2_common/hardware_gpio/gpio.c \
            rp2_common/hardware_uart/uart.c \
            rp2_common/hardware_irq/irq.c \
            rp2_common/hardware_irq/irq_handler_chain.S \
            rp2_common/hardware_timer/timer.c \
            rp2_common/hardware_clocks/clocks.c \
            rp2_common/hardware_pll/pll.c \
            rp2_common/hardware_dma/dma.c \
            rp2_common/hardware_spi/spi.c \
            rp2_common/hardware_i2c/i2c.c \
            rp2_common/hardware_adc/adc.c \
            rp2_common/hardware_pio/pio.c \
            rp2_common/hardware_watchdog/watchdog.c \
            rp2_common/hardware_flash/flash.c \
            rp2_common/pico_unique_id/unique_id.c \
            rp2_common/pico_platform_panic/panic.c \
            rp2_common/pico_platform_common/common.c \
            rp2_common/pico_multicore/multicore.c \
            common/pico_sync/mutex.c \
            common/pico_time/time.c \
            common/pico_sync/lock_core.c \
            common/hardware_claim/claim.c \
            common/pico_sync/critical_section.c \
            rp2_common/hardware_sync/sync.c \
            rp2_common/pico_runtime_init/runtime_init.c \
            rp2_common/pico_runtime_init/runtime_init_clocks.c \
            rp2_common/pico_runtime_init/runtime_init_stack_guard.c \
            rp2_common/pico_runtime/runtime.c \
            rp2_common/hardware_ticks/ticks.c \
            rp2_common/hardware_xosc/xosc.c \
            common/pico_sync/sem.c \
            common/pico_time/timeout_helper.c \
            common/pico_util/pheap.c \
            common/pico_util/queue.c \
            rp2350/pico_platform/platform.c \
            rp2_common/pico_atomic/atomic.c \
            rp2_common/pico_bootrom/bootrom.c \
            rp2_common/pico_bootrom/bootrom_lock.c \
            rp2_common/pico_divider/divider_compiler.c \
            rp2_common/pico_flash/flash.c \
            rp2_common/hardware_divider/divider.c \
            rp2_common/hardware_vreg/vreg.c \
            rp2_common/hardware_xip_cache/xip_cache.c \
            rp2_common/pico_standard_binary_info/standard_binary_info.c \
            rp2_common/pico_malloc/malloc.c \
            rp2_common/pico_stdlib/stdlib.c \
            rp2_common/pico_bit_ops/bit_ops_aeabi.S \
            rp2_common/pico_stdio/stdio.c \
            rp2_common/pico_printf/printf.c \
            rp2_common/pico_fix/rp2040_usb_device_enumeration/rp2040_usb_device_enumeration.c \
            rp2_common/pico_float/float_common_m33.S \
            rp2_common/pico_float/float_conv32_vfp.S \
            rp2_common/pico_float/float_math.c \
            rp2_common/pico_float/float_sci_m33_vfp.S \
            rp2_common/pico_double/double_aeabi_dcp.S \
            rp2_common/pico_double/double_conv_m33.S \
            rp2_common/pico_double/double_fma_dcp.S \
            rp2_common/pico_double/double_math.c \
            rp2_common/pico_double/double_sci_m33.S

TINY_USB_SRC_DIR = $(LIB_MODULES_DIR)/pico-sdk/lib/tinyusb/src
TINYUSB_SRC := \
            $(TINY_USB_SRC_DIR)/tusb.c \
            $(TINY_USB_SRC_DIR)/class/cdc/cdc_device.c \
            $(TINY_USB_SRC_DIR)/common/tusb_fifo.c \
            $(TINY_USB_SRC_DIR)/device/usbd.c \
            $(TINY_USB_SRC_DIR)/device/usbd_control.c \
            $(TINY_USB_SRC_DIR)/portable/raspberrypi/rp2040/dcd_rp2040.c \
            $(TINY_USB_SRC_DIR)/portable/raspberrypi/rp2040/rp2040_usb.c \
            $(TINY_USB_SRC_DIR)/class/msc/msc_device.c

# pico_float wrapped libm functions (route to pico-sdk's optimised FPv5 implementations)
PICO_FLOAT_WRAP_FNS = \
            __aeabi_f2lz __aeabi_f2ulz __aeabi_l2f __aeabi_ul2f \
            acosf acoshf asinf asinhf atan2f atanf atanhf cbrtf ceilf copysignf \
            cosf coshf dremf exp10f exp2f expf expm1f floorf fmaf fmodf hypotf \
            ldexpf log10f log1pf log2f logf powf powintf remainderf remquof \
            roundf sincosf sinf sinhf tanf tanhf truncf

PICO_FLOAT_LD_FLAGS = $(foreach fn, $(PICO_FLOAT_WRAP_FNS), -Wl,--wrap=$(fn))

# pico_double wrapped libm functions
PICO_DOUBLE_WRAP_FNS = \
            __aeabi_cdcmpeq __aeabi_cdcmple __aeabi_cdrcmple __aeabi_d2f __aeabi_d2iz \
            __aeabi_d2lz __aeabi_d2uiz __aeabi_d2ulz __aeabi_dadd __aeabi_dcmpeq \
            __aeabi_dcmpge __aeabi_dcmpgt __aeabi_dcmple __aeabi_dcmplt __aeabi_dcmpun \
            __aeabi_ddiv __aeabi_dmul __aeabi_drsub __aeabi_dsub __aeabi_i2d __aeabi_l2d \
            __aeabi_ui2d __aeabi_ul2d \
            acos acosh asin asinh atan atan2 atanh cbrt ceil copysign cos cosh drem \
            exp exp10 exp2 expm1 floor fma fmod hypot ldexp log log10 log1p log2 pow \
            powint remainder remquo round sin sincos sinh sqrt tan tanh trunc

PICO_DOUBLE_LD_FLAGS = $(foreach fn, $(PICO_DOUBLE_WRAP_FNS), -Wl,--wrap=$(fn))

PICO_STDIO_WRAP_FNS = sprintf snprintf vsnprintf printf vprintf puts putchar getchar
PICO_STDIO_LD_FLAGS = $(foreach fn, $(PICO_STDIO_WRAP_FNS), -Wl,--wrap=$(fn))

PICO_BIT_OPS_LD_FLAGS = -Wl,--wrap=__ctzdi2

PICO_LIB_SRC += drivers/memfunctions.S

PICO_MEM_WRAP_FNS = memcpy_44 memcpy memset_4 memset
PICO_MEM_LD_FLAGS = $(foreach fn, $(PICO_MEM_WRAP_FNS), -Wl,--wrap=$(fn))

EXTRA_LD_FLAGS += $(PICO_STDIO_LD_FLAGS) $(PICO_FLOAT_LD_FLAGS) $(PICO_DOUBLE_LD_FLAGS) $(PICO_BIT_OPS_LD_FLAGS) $(PICO_MEM_LD_FLAGS)

INCLUDE_DIRS += \
            $(ROOT)/src/main/drivers \
            $(ROOT)/lib/main/STM32_USB_Device_Library/Core/inc \
            $(ROOT)/lib/main/STM32_USB_Device_Library/Class/msc/inc \
            $(ROOT)/src/main/drivers/rp2350_config

SYS_INCLUDE_DIRS = \
            $(SDK_DIR)/common/pico_bit_ops_headers/include \
            $(SDK_DIR)/common/pico_base_headers/include \
            $(SDK_DIR)/common/boot_picoboot_headers/include \
            $(SDK_DIR)/common/pico_usb_reset_interface_headers/include \
            $(SDK_DIR)/common/pico_time/include \
            $(SDK_DIR)/common/boot_uf2_headers/include \
            $(SDK_DIR)/common/pico_divider_headers/include \
            $(SDK_DIR)/common/boot_picobin_headers/include \
            $(SDK_DIR)/common/pico_util/include \
            $(SDK_DIR)/common/pico_stdlib_headers/include \
            $(SDK_DIR)/common/hardware_claim/include \
            $(SDK_DIR)/common/pico_binary_info/include \
            $(SDK_DIR)/common/pico_sync/include \
            $(SDK_DIR)/rp2_common/pico_stdio_uart/include \
            $(SDK_DIR)/rp2_common/pico_stdio_usb/include \
            $(SDK_DIR)/rp2_common/tinyusb/include \
            $(SDK_DIR)/rp2_common/hardware_rtc/include \
            $(SDK_DIR)/rp2_common/hardware_boot_lock/include \
            $(SDK_DIR)/rp2_common/pico_mem_ops/include \
            $(SDK_DIR)/rp2_common/hardware_exception/include \
            $(SDK_DIR)/rp2_common/hardware_sync_spin_lock/include \
            $(SDK_DIR)/rp2_common/pico_runtime_init/include \
            $(SDK_DIR)/rp2_common/pico_standard_link/include \
            $(SDK_DIR)/rp2_common/hardware_pio/include \
            $(SDK_DIR)/rp2_common/pico_platform_compiler/include \
            $(SDK_DIR)/rp2_common/hardware_divider/include \
            $(SDK_DIR)/rp2_common/hardware_dcp/include \
            $(SDK_DIR)/rp2_common/pico_bootsel_via_double_reset/include \
            $(SDK_DIR)/rp2_common/hardware_powman/include \
            $(SDK_DIR)/rp2_common/hardware_flash/include \
            $(SDK_DIR)/rp2_common/hardware_ticks/include \
            $(SDK_DIR)/rp2_common/hardware_dma/include \
            $(SDK_DIR)/rp2_common/pico_bit_ops/include \
            $(SDK_DIR)/rp2_common/hardware_clocks/include \
            $(SDK_DIR)/rp2_common/pico_unique_id/include \
            $(SDK_DIR)/rp2_common/hardware_watchdog/include \
            $(SDK_DIR)/rp2_common/pico_rand/include \
            $(SDK_DIR)/rp2_common/hardware_hazard3/include \
            $(SDK_DIR)/rp2_common/hardware_uart/include \
            $(SDK_DIR)/rp2_common/hardware_interp/include \
            $(SDK_DIR)/rp2_common/pico_printf/include \
            $(SDK_DIR)/rp2_common/pico_aon_timer/include \
            $(SDK_DIR)/rp2_common/hardware_riscv_platform_timer/include \
            $(SDK_DIR)/rp2_common/pico_double/include \
            $(SDK_DIR)/rp2_common/hardware_vreg/include \
            $(SDK_DIR)/rp2_common/hardware_spi/include \
            $(SDK_DIR)/rp2_common/hardware_rcp/include \
            $(SDK_DIR)/rp2_common/hardware_riscv/include \
            $(SDK_DIR)/rp2_common/pico_standard_binary_info/include \
            $(SDK_DIR)/rp2_common/pico_i2c_slave/include \
            $(SDK_DIR)/rp2_common/pico_int64_ops/include \
            $(SDK_DIR)/rp2_common/hardware_irq/include \
            $(SDK_DIR)/rp2_common/pico_divider/include \
            $(SDK_DIR)/rp2_common/pico_flash/include \
            $(SDK_DIR)/rp2_common/hardware_sync/include \
            $(SDK_DIR)/rp2_common/pico_bootrom/include \
            $(SDK_DIR)/rp2_common/pico_crt0/include \
            $(SDK_DIR)/rp2_common/pico_clib_interface/include \
            $(SDK_DIR)/rp2_common/pico_stdio/include \
            $(SDK_DIR)/rp2_common/pico_runtime/include \
            $(SDK_DIR)/rp2_common/pico_time_adapter/include \
            $(SDK_DIR)/rp2_common/pico_platform_panic/include \
            $(SDK_DIR)/rp2_common/pico_platform_common/include \
            $(SDK_DIR)/rp2_common/hardware_adc/include \
            $(SDK_DIR)/rp2_common/cmsis/include \
            $(SDK_DIR)/rp2_common/hardware_pll/include \
            $(SDK_DIR)/rp2_common/pico_platform_sections/include \
            $(SDK_DIR)/rp2_common/boot_bootrom_headers/include \
            $(SDK_DIR)/rp2_common/pico_fix/include \
            $(SDK_DIR)/rp2_common/hardware_base/include \
            $(SDK_DIR)/rp2_common/hardware_xosc/include \
            $(SDK_DIR)/rp2_common/pico_async_context/include \
            $(SDK_DIR)/rp2_common/hardware_pwm/include \
            $(SDK_DIR)/rp2_common/pico_stdio_semihosting/include \
            $(SDK_DIR)/rp2_common/pico_float/include \
            $(SDK_DIR)/rp2_common/hardware_resets/include \
            $(SDK_DIR)/rp2_common/pico_cxx_options/include \
            $(SDK_DIR)/rp2_common/pico_stdlib/include \
            $(SDK_DIR)/rp2_common/hardware_i2c/include \
            $(SDK_DIR)/rp2_common/pico_atomic/include \
            $(SDK_DIR)/rp2_common/pico_multicore/include \
            $(SDK_DIR)/rp2_common/hardware_gpio/include \
            $(SDK_DIR)/rp2_common/pico_malloc/include \
            $(SDK_DIR)/rp2_common/hardware_timer/include \
            $(SDK_DIR)/rp2_common/hardware_xip_cache/include \
            $(CMSIS_DIR)/Core/Include \
            $(CMSIS_DIR)/Device/$(TARGET_MCU_LIB_UPPER)/Include \
            $(SDK_DIR)/$(TARGET_MCU_LIB_LOWER)/pico_platform/include \
            $(SDK_DIR)/$(TARGET_MCU_LIB_LOWER)/hardware_regs/include \
            $(SDK_DIR)/$(TARGET_MCU_LIB_LOWER)/hardware_structs/include \
            $(SDK_DIR)/rp2350/boot_stage2/include \
            $(SDK_DIR)/rp2_common/pico_fix/rp2040_usb_device_enumeration/include \
            $(LIB_MODULES_DIR)/pico-sdk/lib/tinyusb/src

# Deliberately NOT merged into INCLUDE_DIRS: the root Makefile passes
# SYS_INCLUDE_DIRS via -isystem (not -I), which is more tolerant of benign
# macro redefinitions across pico-sdk's own headers - mirrors betaflight's own
# RP2350.mk workaround for https://github.com/raspberrypi/pico-sdk/issues/2451.

#Flags
ARCH_FLAGS      = -mthumb -mcpu=cortex-m33 -march=armv8-m.main+fp+dsp -mcmse -mfloat-abi=softfp
ARCH_FLAGS      += -DPICO_COPY_TO_RAM=$(RUN_FROM_RAM)

# Work around memcpy alignment issue: compiler to generate function calls
# rather than inlining code that is sometimes broken.
ARCH_FLAGS      += -fno-builtin-memcpy -fno-builtin-memset

DEVICE_FLAGS    += \
            -DPICO_RP2350_A2_SUPPORTED=1 \
            -DLIB_BOOT_STAGE2_HEADERS=1 \
            -DLIB_PICO_ATOMIC=1 \
            -DLIB_PICO_BIT_OPS=1 \
            -DLIB_PICO_BIT_OPS_PICO=1 \
            -DLIB_PICO_CLIB_INTERFACE=1 \
            -DLIB_PICO_CRT0=1 \
            -DLIB_PICO_CXX_OPTIONS=1 \
            -DLIB_PICO_DIVIDER=1 \
            -DLIB_PICO_DIVIDER_COMPILER=1 \
            -DLIB_PICO_DOUBLE=1 \
            -DLIB_PICO_DOUBLE_PICO=1 \
            -DLIB_PICO_FLOAT=1 \
            -DLIB_PICO_FLOAT_PICO=1 \
            -DLIB_PICO_FLOAT_PICO_VFP=1 \
            -DLIB_PICO_INT64_OPS=1 \
            -DLIB_PICO_INT64_OPS_COMPILER=1 \
            -DLIB_PICO_MALLOC=1 \
            -DLIB_PICO_MEM_OPS=1 \
            -DLIB_PICO_MEM_OPS_COMPILER=1 \
            -DLIB_PICO_NEWLIB_INTERFACE=1 \
            -DLIB_PICO_PLATFORM=1 \
            -DLIB_PICO_PLATFORM_COMMON=1 \
            -DLIB_PICO_PLATFORM_COMPILER=1 \
            -DLIB_PICO_PLATFORM_PANIC=1 \
            -DLIB_PICO_PLATFORM_SECTIONS=1 \
            -DLIB_PICO_PRINTF=1 \
            -DLIB_PICO_PRINTF_PICO=1 \
            -DLIB_PICO_RUNTIME=1 \
            -DLIB_PICO_RUNTIME_INIT=1 \
            -DLIB_PICO_STANDARD_BINARY_INFO=1 \
            -DLIB_PICO_STANDARD_LINK=1 \
            -DLIB_PICO_STDIO=1 \
            -DLIB_PICO_STDIO_UART=1 \
            -DLIB_PICO_STDLIB=1 \
            -DLIB_PICO_SYNC=1 \
            -DLIB_PICO_SYNC_CRITICAL_SECTION=1 \
            -DLIB_PICO_SYNC_MUTEX=1 \
            -DLIB_PICO_SYNC_SEM=1 \
            -DLIB_PICO_TIME=1 \
            -DLIB_PICO_TIME_ADAPTER=1 \
            -DLIB_PICO_UTIL=1 \
            -DPICO_32BIT=1 \
            -DPICO_BUILD=1 \
            -DPICO_CXX_ENABLE_EXCEPTIONS=0 \
            -DPICO_NO_FLASH=0 \
            -DPICO_NO_HARDWARE=0 \
            -DPICO_ON_DEVICE=1 \
            -DPICO_RP2350=1 \
            -DPICO_USE_BLOCKED_RAM=0 \
            -DPICO_CORE1_STACK_SIZE=0x1000 \
            -DPICO

DEVICE_FLAGS    += \
            -DLIB_PICO_PRINTF=1 \
            -DLIB_PICO_PRINTF_PICO=1  \
            -DLIB_PICO_STDIO=1  \
            -DLIB_PICO_STDIO_USB=1 \
            -DCFG_TUSB_DEBUG=0  \
            -DCFG_TUSB_MCU=OPT_MCU_RP2040  \
            -DCFG_TUSB_OS=OPT_OS_NONE  \
            -DLIB_PICO_FIX_RP2040_USB_DEVICE_ENUMERATION=1 \
            -DPICO_RP2040_USB_DEVICE_UFRAME_FIX=1  \
            -DPICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=3000 \
            -DLIB_PICO_UNIQUEID=1

TARGET_FLAGS    = -D$(TARGET)

# LD_SCRIPT must be set to the first included linker script.
LD_SCRIPT = $(LINKER_DIR)/pico_flash_mem_defaults.ld

# Override pico_flash_mem_defaults.ld's PRIMARY_FLASH_LENGTH default (4M) with
# this target's actual physical flash size (MCU_FLASH_SIZE, in KB, set per
# variant above: 8192 for RP2350A/B's external QSPI, 2048 for RP2354A/B's
# on-die flash) - see pico_flash_mem_defaults.ld for why this must be a real
# per-target value rather than a shared guess. --defsym takes effect as if
# inserted at the very start of the link, so it doesn't matter that this
# appears before pico_flash_mem_defaults.ld's own (now-conditional) assignment.
EXTRA_LD_FLAGS  += -Wl,--defsym=PRIMARY_FLASH_LENGTH=$(shell echo $$(( $(MCU_FLASH_SIZE) * 1024 )))

ifeq ($(RUN_FROM_RAM),1)
# RunFromHybrid -> load most code / data into RAM, with some exclusions (cli, pg, ...)
EXTRA_LD_FLAGS  += -T$(LINKER_DIR)/pico_rp2350_RunFromHybrid.ld
else
EXTRA_LD_FLAGS  += -T$(LINKER_DIR)/pico_rp2350_RunFromFLASH.ld
endif

# Linker script pico_rp2350_RunFromHybrid.ld assigns certain symbols into flash
# instead of RAM, but that can't work if the build uses LTO (which breaks files
# up into temporary objects), so disable LTO for RAM-based builds like betaflight does.
ifeq ($(RUN_FROM_RAM),1)
LTO := no
endif

# Override the OPTIMISE_SPEED compiler setting to save flash space (mirrors betaflight RP2350.mk).
OPTIMISE_SPEED  = -O2

VCP_SRC = \
            drivers/usb_io.c \
            drivers/usb_pico/usb_cdc.c \
            drivers/usb_pico/usb_descriptors.c \
            drivers/serial_usb_vcp_pico.c

MCU_COMMON_SRC = \
            drivers/dshot_bitbang_decode.c \
            drivers/bus_i2c_utils.c \
            drivers/adc_pico.c \
            drivers/bus_i2c_pico.c \
            drivers/bus_spi_pico.c \
            drivers/bus_quadspi_pico.c \
            drivers/debug_pico.c \
            drivers/dma_pico.c \
            drivers/dshot_bidir_pico.c \
            drivers/dshot_pico.c \
            drivers/exti_pico.c \
            drivers/persistent_rp2350.c \
            drivers/pwm_motor_pico.c \
            drivers/pwm_servo_pico.c \
            drivers/pwm_beeper_pico.c \
            drivers/gyro_clkin_pico.c \
            drivers/system_rp2350.c \
            drivers/serial_uart_pico.c \
            drivers/multicore.c \
            drivers/io_pico.c \
            drivers/light_ws2811strip_pico.c

# Files replaced by the RP2350-specific equivalents above.
# timer.c/timer_common.c/dma_reqmap.c are the STM32 hardware-timer and
# DMAMUX-request-map abstractions (TIM_TypeDef-based) - RP2350_UNIFIED's
# target.h already #undefs USE_TIMER/USE_DMA_SPEC/USE_TIMER_MGMT (PICO uses
# PIO-based dshot/PWM/UART instead), so every caller of dma_reqmap's API is
# already compiled out for PICO; excluding these 3 files avoids them pulling
# in drivers/timer.h (which has no PICO branch and isn't needed here).
MCU_EXCLUDES = \
            drivers/persistent.c \
            drivers/system.c \
            drivers/exti.c \
            drivers/rcc.c \
            drivers/rx/rx_pwm.c \
            drivers/timer.c \
            drivers/timer_common.c \
            drivers/dma_reqmap.c \
            drivers/dshot_dpwm.c \
            drivers/pwm_output_dshot.c \
            drivers/pwm_output_dshot_shared.c \
            drivers/pwm_output_dshot_hal.c \
            drivers/pwm_output_dshot_hal_hal.c

# USB-MSC over TinyUSB. usb_msc_pico.c implements the tud_msc_* callbacks
# against the generic USBD_STORAGE_cb_TypeDef fops interface and carries its
# own internal SD-SPI backend, so msc/usbd_storage_sd_spi.c must NOT be
# listed here (it would duplicate USBD_MSC_MICRO_SD_SPI_fops); the emfat
# flash-log backend (msc/usbd_storage_emfat.c + emfat*.c) is shared with
# STM32 unchanged. NB source.mk appends $(MSC_SRC) under the SDCARD_SPI and
# ONBOARDFLASH feature blocks (both enabled in RP2350_UNIFIED/target.mk) -
# duplicate entries are collapsed by make's $^ in the link rule.
MSC_SRC = \
            drivers/usb_msc_common.c \
            drivers/usb_pico/usb_msc_pico.c \
            msc/usbd_storage_emfat.c \
            msc/emfat.c \
            msc/emfat_file.c

DEVICE_STDPERIPH_SRC := \
            $(PICO_LIB_SRC) \
            $(STDPERIPH_SRC) \
            $(TINYUSB_SRC)

# pico-sdk/tinyusb objects are compiled with different optimisation flags
# (matches betaflight RP2350.mk - avoids interaction with -Wl,--wrap=... and -flto=auto).
PICO_LIB_OBJS = $(addsuffix .o, $(basename $(PICO_LIB_SRC)))
PICO_LIB_TARGETS := $(foreach pobj, $(PICO_LIB_OBJS), %/$(pobj))
$(PICO_LIB_TARGETS): CC_DEFAULT_OPTIMISATION := $(PICO_LIB_OPTIMISATION)
