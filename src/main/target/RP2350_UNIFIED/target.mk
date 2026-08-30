#
# RP2350/RP2354 unified target dispatcher.
# TARGET is one of RP2350A, RP2350B, RP2354A, RP2354B (see the empty *.mk
# alt-target marker files in this directory). This file sets the group
# variable (consumed by make/targets.mk to pick TARGET_MCU := RP2350) and
# per-chip flash/device flags, mirroring src/main/target/STM32_UNIFIED/target.mk.
#

ifneq ($(findstring RP2350A,$(TARGET)),)
RP2350_TARGETS  += $(TARGET)
DEVICE_FLAGS    += -DPICO_RP2350A=1
# External QSPI flash - the part is board dependent and cannot be known at build
# time, so this defaults to the smallest size in common use (4MB, as fitted to
# the Raspberry Pi Pico 2) rather than the largest.
#
# The two directions are NOT equally safe. FLASH_CONFIG and FLASH_CUSTOM_DEFAULTS
# are placed at the END of flash (src/link/pico_rp2350_memory.ld), so:
#   - Under-declaring is harmless: config lands inside the chip and the space
#     above it is simply unused.
#   - Over-declaring is silently destructive: config_streamer.c's
#     flash_range_erase()/flash_range_program() target addresses past the end of
#     the physical device.
# A default of 8MB therefore produced a broken image for a 4MB board from a plain
# `make TARGET=RP2350A`, with no build-time diagnostic.
#
# Boards with a larger part should opt in explicitly to use it, e.g.:
#   make TARGET=RP2350A MCU_FLASH_SIZE=8192
# PICO_FLASH_SIZE_BYTES is derived from this so the pico-sdk's flash range checks
# and the linker's end-of-flash placement stay in sync.
MCU_FLASH_SIZE  ?= 4096
PICO_FLASH_DEFINES = \
                   -DPICO_FLASH_SPI_CLKDIV=2 \
                   -DPICO_FLASH_SIZE_BYTES=$(shell echo $$(( $(MCU_FLASH_SIZE) * 1024 ))) \
                   -DPICO_BOOT_STAGE2_CHOOSE_W25Q080=1
FEATURES        += VCP SDCARD_SPI ONBOARDFLASH
endif

ifneq ($(findstring RP2350B,$(TARGET)),)
RP2350_TARGETS  += $(TARGET)
# In pico-sdk, PICO_RP2350A=0 means RP2350B family.
DEVICE_FLAGS    += -DPICO_RP2350A=0
# External QSPI flash - the part is board dependent and cannot be known at build
# time, so this defaults to the smallest size in common use (4MB, as fitted to
# the Raspberry Pi Pico 2) rather than the largest.
#
# The two directions are NOT equally safe. FLASH_CONFIG and FLASH_CUSTOM_DEFAULTS
# are placed at the END of flash (src/link/pico_rp2350_memory.ld), so:
#   - Under-declaring is harmless: config lands inside the chip and the space
#     above it is simply unused.
#   - Over-declaring is silently destructive: config_streamer.c's
#     flash_range_erase()/flash_range_program() target addresses past the end of
#     the physical device.
# A default of 8MB therefore produced a broken image for a 4MB board from a plain
# `make TARGET=RP2350A`, with no build-time diagnostic.
#
# Boards with a larger part should opt in explicitly to use it, e.g.:
#   make TARGET=RP2350A MCU_FLASH_SIZE=8192
# PICO_FLASH_SIZE_BYTES is derived from this so the pico-sdk's flash range checks
# and the linker's end-of-flash placement stay in sync.
MCU_FLASH_SIZE  ?= 4096
PICO_FLASH_DEFINES = \
                   -DPICO_FLASH_SPI_CLKDIV=2 \
                   -DPICO_FLASH_SIZE_BYTES=$(shell echo $$(( $(MCU_FLASH_SIZE) * 1024 ))) \
                   -DPICO_BOOT_STAGE2_CHOOSE_W25Q080=1
FEATURES        += VCP SDCARD_SPI ONBOARDFLASH
endif

ifneq ($(findstring RP2354A,$(TARGET)),)
RP2350_TARGETS  += $(TARGET)
DEVICE_FLAGS    += -DPICO_RP2350A=1
# On-die 2MB flash, memory-mapped at the same XIP window as external QSPI flash.
# TODO: verify boot_stage2/QMI defaults for on-die flash against pico-sdk before real hardware bring-up.
MCU_FLASH_SIZE  := 2048
PICO_FLASH_DEFINES = \
                   -DPICO_FLASH_SIZE_BYTES=2097152
FEATURES        += VCP SDCARD_SPI ONBOARDFLASH
endif

ifneq ($(findstring RP2354B,$(TARGET)),)
RP2350_TARGETS  += $(TARGET)
DEVICE_FLAGS    += -DPICO_RP2350A=0
# On-die 2MB flash, memory-mapped at the same XIP window as external QSPI flash.
# TODO: verify boot_stage2/QMI defaults for on-die flash against pico-sdk before real hardware bring-up.
MCU_FLASH_SIZE  := 2048
PICO_FLASH_DEFINES = \
                   -DPICO_FLASH_SIZE_BYTES=2097152
FEATURES        += VCP SDCARD_SPI ONBOARDFLASH
endif

DEVICE_FLAGS    += $(PICO_FLASH_DEFINES)

# Every accgyro/barometer/compass driver is compiled in and runtime-detected
# from the CLI config (resource GYRO_CS/GYRO_EXTI, gyro_1_bustype,
# baro_hardware, mag_hardware, ...) - no board-specific code needed here,
# mirroring src/main/target/STM32_UNIFIED/target.mk's identical pattern.
TARGET_SRC = \
    $(addprefix drivers/accgyro/,$(notdir $(wildcard $(SRC_DIR)/drivers/accgyro/*.c))) \
    $(ROOT)/lib/main/BoschSensortec/BMI270-Sensor-API/bmi270_maximum_fifo.c \
    $(addprefix drivers/barometer/,$(notdir $(wildcard $(SRC_DIR)/drivers/barometer/*.c))) \
    $(addprefix drivers/compass/,$(notdir $(wildcard $(SRC_DIR)/drivers/compass/*.c)))
