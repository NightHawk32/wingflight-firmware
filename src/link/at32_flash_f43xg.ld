/*
*****************************************************************************
**
**  File        : at32_flash_f43xg.ld
**
**  Abstract    : Linker script for AT32F435/437 devices with
**                1024KByte FLASH, 384KByte SRAM (192K used, see note below).
**
*****************************************************************************
*/

/*
0x08000000 to 0x080FFFFF 1024K full flash,
0x08000000 to 0x08003FFF   16K isr vector, startup code,
0x08004000 to 0x08007FFF   16K config,
0x08008000 to 0x080FFFFF  992K firmware,
*/

/* Specify the memory areas */
MEMORY
{
    FLASH (rx)        : ORIGIN = 0x08000000, LENGTH = 10K
    FLASH_CUSTOM_DEFAULTS (r) : ORIGIN = 0x08002800, LENGTH = 6K
    FLASH_CONFIG (r)  : ORIGIN = 0x08004000, LENGTH = 16K
    FLASH1 (rx)       : ORIGIN = 0x08008000, LENGTH = DEFINED(USE_CUSTOM_DEFAULTS_EXTENDED) ? 976K : 992K
    FLASH_CUSTOM_DEFAULTS_EXTENDED (r): ORIGIN = DEFINED(USE_CUSTOM_DEFAULTS_EXTENDED) ? 0x080FC000 : 0x08100000, LENGTH = DEFINED(USE_CUSTOM_DEFAULTS_EXTENDED) ? 16K : 0K

    /* AT32F435/437 system memory (boot ROM/ISP) base -- NOT independently verified against
     * the AT32F435 datasheet yet, only used to reserve address space for
     * checkForBootLoaderRequest()'s runtime jump target. Does not affect normal boot/build. */
    SYSTEM_MEMORY (rx): ORIGIN = 0x1FFF0000, LENGTH = 29K

    /* AT32F435/437 has 384K SRAM total, but only a 192K contiguous bank is used here,
     * matching betaflight's own working AT32 linker script (src/platform/AT32/link/
     * at32_flash_f43xg.ld) -- no separate CCM/DTCM-style fast-RAM bank exists on this MCU,
     * so STACKRAM/FASTRAM/VECTAB all alias to the single RAM region below (same pattern as
     * stm32_flash_f411.ld, which likewise has no CCM). */
    RAM (rwx)         : ORIGIN = 0x20000000, LENGTH = 192K
    MEMORY_B1 (rx)    : ORIGIN = 0x60000000, LENGTH = 0K
}

REGION_ALIAS("STACKRAM", RAM)
REGION_ALIAS("FASTRAM", RAM)
REGION_ALIAS("VECTAB", RAM)

REGION_ALIAS("MOVABLE_FLASH", FLASH1)

INCLUDE "stm32_flash_f4_split.ld"
