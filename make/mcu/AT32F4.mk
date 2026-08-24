#
# AT32F4 Make file include
#
# STATUS: work in progress (see AT32F435_TODO.md). Several files listed in
# MCU_COMMON_SRC below do not exist yet in src/main/drivers and must be ported
# from betaflight/src/platform/AT32/* before this will build.
#

CMSIS_DIR       := $(ROOT)/lib/main/AT32F43x/cmsis
STDPERIPH_DIR    = $(ROOT)/lib/main/AT32F43x/drivers
MIDDLEWARES_DIR  = $(ROOT)/lib/main/AT32F43x/middlewares

STDPERIPH_SRC    = $(notdir $(wildcard $(STDPERIPH_DIR)/src/*.c))

MCU_FLASH_SIZE  := 1024

DEVICE_STDPERIPH_SRC := $(STDPERIPH_SRC)

VPATH           := $(VPATH):$(STDPERIPH_DIR)/src:$(CMSIS_DIR)/cm4/core_support:$(MIDDLEWARES_DIR)/i2c_application_library
# NOTE: VPATH points at $(MIDDLEWARES_DIR) itself (not a subdirectory) -- source file
# names in MCU_COMMON_SRC/VCP_SRC/MSC_SRC below (e.g. usb_drivers/src/usb_core.c,
# usbd_class/cdc/cdc_class.c) already include their full path relative to it.
VPATH           := $(VPATH):$(MIDDLEWARES_DIR)

# NOTE: $(ROOT)/src/main/drivers is listed BEFORE $(MIDDLEWARES_DIR)/usbd_class/msc
# deliberately -- drivers/usb_conf.h and drivers/msc_desc.h override Artery's own
# usb_conf.h (no vendor default exists) / usbd_class/msc/msc_desc.h (vendor default
# exists but must be shadowed with our own product strings, since msc_class.c/
# msc_desc.c #include <msc_desc.h> with angle brackets -- see drivers/msc_desc.h).
INCLUDE_DIRS    := $(INCLUDE_DIRS) \
                   $(STDPERIPH_DIR)/inc \
                   $(CMSIS_DIR)/cm4 \
                   $(CMSIS_DIR)/cm4/core_support \
                   $(CMSIS_DIR)/cm4/device_support \
                   $(MIDDLEWARES_DIR)/i2c_application_library \
                   $(ROOT)/src/main/drivers \
                   $(MIDDLEWARES_DIR)/usb_drivers/inc \
                   $(MIDDLEWARES_DIR)/usbd_class/cdc \
                   $(MIDDLEWARES_DIR)/usbd_class/msc

ifneq ($(filter SDCARD_SPI,$(FEATURES)),)
INCLUDE_DIRS    := $(INCLUDE_DIRS) \
                   $(FATFS_DIR)
VPATH           := $(VPATH):$(FATFS_DIR)
endif

#Flags
ARCH_FLAGS      = -mthumb -mcpu=cortex-m4 -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16 -fsingle-precision-constant

# AT32F435RGT7 (LQFP100, 1024K flash "G" variant) selects AT32F435xx/AT32F435Rx internally
# via lib/main/AT32F43x/cmsis/cm4/device_support/at32f435_437.h -- do not also pre-define
# AT32F435xx/AT32F435Rx here, the vendor header derives them from the specific part macro.
DEVICE_FLAGS    = -DAT32F43x -DAT32F435RGT7 -DUSE_ATBSP_DRIVER
DEVICE_FLAGS    += -DHSE_VALUE=$(HSE_VALUE)

LD_SCRIPT       = $(LINKER_DIR)/at32_flash_f43xg.ld
STARTUP_SRC     = startup_at32f435_437.s

# NOTE: driver files below are the planned AT32 equivalents of the STM32F4 files in
# make/mcu/STM32F4.mk. Most still need to be ported (see AT32F435_TODO.md section 3).
MCU_COMMON_SRC = \
            startup/system_at32f435_437.c \
            startup/at32f435_437_clock.c \
            drivers/accgyro/accgyro_mpu.c \
            drivers/adc_at32f43x.c \
            drivers/bus_i2c_at32bsp.c \
            drivers/bus_i2c_timing.c \
            drivers/bus_spi_at32bsp.c \
            i2c_application.c \
            drivers/dma_at32f43x.c \
            drivers/dshot_bitbang.c \
            drivers/dshot_bitbang_at32bsp.c \
            drivers/dshot_bitbang_decode.c \
            drivers/inverter.c \
            drivers/light_ws2811strip_at32f43x.c \
            drivers/persistent.c \
            drivers/pwm_output_dshot_at32bsp.c \
            drivers/pwm_output_dshot_shared.c \
            drivers/serial_uart_at32bsp.c \
            drivers/serial_uart_at32f43x.c \
            drivers/system_at32f43x.c \
            drivers/timer_at32bsp.c \
            drivers/timer_at32f43x.c \
            usb_drivers/src/usb_core.c \
            usb_drivers/src/usbd_core.c \
            usb_drivers/src/usbd_int.c \
            usb_drivers/src/usbd_sdr.c

# drivers/timer.c is deeply coupled to STM32 StdPeriph API/register names and cannot be
# shimmed incrementally for AT32 -- drivers/timer_at32bsp.c (above) is a full parallel
# logic port using AT-BSP calls instead, exactly mirroring how drivers/timer_hal.c
# replaces drivers/timer.c for the STM32F7/H7/G4 HAL platforms (see their MCU_EXCLUDES).
MCU_EXCLUDES = \
            drivers/timer.c \
            drivers/serial_usb_vcp.c

# drivers/serial_usb_vcp.c is excluded above: drivers/serial_usb_vcp_at32f4.c (below) is
# a full self-contained replacement (own usbVcpOpen()/vTable) since Artery's usbd_core
# stack is architecturally too different from ST's USB device library to shim via a
# small #elif branch in the shared file, mirroring the timer.c/timer_at32bsp.c precedent.
VCP_SRC = \
            drivers/serial_usb_vcp_at32f4.c \
            drivers/usb_io.c \
            usbd_class/cdc/cdc_class.c \
            usbd_class/cdc/cdc_desc.c

MSC_SRC = \
            drivers/usb_msc_common.c \
            drivers/usb_msc_at32f43x.c \
            msc/usbd_storage.c \
            usbd_class/msc/msc_class.c \
            usbd_class/msc/msc_bot_scsi.c \
            usbd_class/msc/msc_desc.c

ifneq ($(filter SDCARD_SPI,$(FEATURES)),)
MSC_SRC += \
            msc/usbd_storage_sd_spi.c
endif

ifneq ($(filter ONBOARDFLASH,$(FEATURES)),)
MSC_SRC += \
            msc/usbd_storage_emfat.c \
            msc/emfat.c \
            msc/emfat_file.c
endif

DSP_LIB := $(ROOT)/lib/main/CMSIS/DSP
# NOTE: unlike STM32F4.mk, __FPU_PRESENT is NOT pre-defined here -- AT32's own
# at32f435_437.h unconditionally defines __FPU_PRESENT as 1U (STM32's stm32f4xx.h defines
# it as plain 1, which is why STM32F4.mk's identical -D__FPU_PRESENT=1 doesn't conflict);
# pre-defining it here with a different token (1 vs 1U) trips a redefinition -Werror.
DEVICE_FLAGS += -DARM_MATH_MATRIX_CHECK -DARM_MATH_ROUNDING -DUNALIGNED_SUPPORT_DISABLE -DARM_MATH_CM4
