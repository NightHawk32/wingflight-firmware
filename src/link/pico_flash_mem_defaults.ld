/* Default linker file definitions for size of flash and size allocated to fonts.
 * If pico_flash_mem.ld is present in the config folder, that will be used instead.
 *
 * Both assignments are conditional (DEFINED() checks) rather than plain `=`,
 * so a value already established via `-Wl,--defsym=...` on the link command
 * line - as make/mcu/RP2350.mk does, deriving PRIMARY_FLASH_LENGTH from the
 * per-target MCU_FLASH_SIZE (RP2350A/B: 8MB external QSPI, RP2354A/B: 2MB
 * on-die) - is not silently clobbered by this file's default. Without this,
 * every RP2350/RP2354 variant shared one hardcoded 4M assumption regardless
 * of actual physical flash size: correct for neither, and actively unsafe on
 * RP2354 (its FLASH_CONFIG/FLASH_FONT regions would be computed from a 4M
 * flash that doesn't physically exist on a 2MB chip - config_streamer.c's
 * flash_range_erase()/flash_range_program() calls would target an address
 * for hardware that isn't there).
 */

/* Set the size of the primary flash. */
PRIMARY_FLASH_LENGTH = DEFINED(PRIMARY_FLASH_LENGTH) ? PRIMARY_FLASH_LENGTH : 4M;

/* Set the size allocated to fonts (for FB_OSD). */
FONT_LENGTH = DEFINED(FONT_LENGTH) ? FONT_LENGTH : 0;
