# RP2350 Porting Plan & Progress Tracker

Status date: 2026-08-24
Owner: (assign when work starts)

This document tracks the plan to add Raspberry Pi RP2350/RP2354 (Cortex-M33)
support to Wingflight, porting from the vendored upstream Betaflight snapshot in
[betaflight/](../betaflight/), specifically `betaflight/src/platform/PICO/`.

Decided target variants (see Phase 0): **all four** — `RP2350A`, `RP2350B`
(external QSPI flash) and `RP2354A`, `RP2354B` (same packages/pinouts as their
RP2350 counterparts, built-in on-die flash). "A" parts are QFN-60/30 GPIO,
"B" parts are QFN-80/48 GPIO.

Update the checkboxes and the "Progress Log" section as work proceeds. Do not
delete completed items — leave them checked so history is preserved.

## Why this is a large port (context for future readers)

- Wingflight (Rotorflight / Betaflight 4.3 lineage) uses the **old flat build**:
  all MCU drivers live directly in [src/main/drivers](../src/main/drivers), one
  [src/main/target](../src/main/target)/`<TARGET>` per board, and per-family
  rules in [make/mcu](../make/mcu) (`STM32F4.mk`, `STM32H7.mk`, ...).
- The vendored `betaflight/` snapshot uses a **fully refactored, platform-abstracted
  build**: `betaflight/src/platform/<NAME>/{drivers,target,mk,link,include}`.
  RP2350 support lives entirely under `betaflight/src/platform/PICO/`.
- There is no `src/platform` layer in Wingflight to drop PICO into. Every ported
  file must be re-hosted into the old layout, and checked against Wingflight's
  older driver interfaces (`adc.h`, `dma.h`, `timer.h`, `motor.h`,
  `pwm_output.h`, `serial_uart.h`) rather than assumed to be a drop-in copy.
- Good news: Wingflight targets are already "unified target" style (runtime CLI
  resource config, see `USE_UNIFIED_TARGET` in
  [src/main/target/STM32_UNIFIED/target.h](../src/main/target/STM32_UNIFIED/target.h)),
  and Betaflight's PICO target headers (`target/RP2350A/target.h`,
  `target/common/target_RP2350.h`) are similarly thin/flag-based, so the
  *target definition* philosophy lines up well even though the *driver* code
  does not.
- pico-sdk + tinyusb are git submodules in `betaflight/.gitmodules`
  (`lib/modules/pico-sdk`). Wingflight has **no `.gitmodules`** — it vendors
  library sources directly under `lib/main/<FAMILY>` instead.
  **Decision (Phase 0): Wingflight will adopt git submodules for pico-sdk
  (+ tinyusb)**, matching Betaflight's approach, so this repo will gain its
  first `.gitmodules` file.
- `betaflight/src/platform/PICO/` only supports `RP2350A`/`RP2350B` — there is
  **no existing upstream RP2354 support to port**. RP2354A/RP2354B are pin-
  and package-compatible with RP2350A/RP2350B respectively (same die/package
  per pair) but add on-die flash (2MB), so they do not need external QSPI
  flash wiring. The RP2354A/RP2354B targets will each be derived from their
  ported RP2350A/RP2350B counterpart rather than copied from betaflight, with
  flash/boot config adjusted for on-die flash (pico-sdk itself already has
  RP2354 register/CMSIS support upstream, it's just Betaflight's PICO platform
  mk/target files that stop at RP2350).

## Source material to port from (reference only, do not edit betaflight/)

- `betaflight/src/platform/PICO/` — drivers (ADC, I2C, SPI, QuadSPI, DMA, EXTI,
  GPIO/io, UART incl. PIO-based UART, USB CDC/MSC via TinyUSB, DSHOT via PIO,
  PWM motor/servo/beeper, persistent config-in-flash, OSD framebuffer,
  multicore, system/boot).
- `betaflight/src/platform/PICO/mk/RP2350.mk` — master build file (pico-sdk
  source list, TinyUSB source list, float/double software-wrap linker flags,
  `ARCH_FLAGS = -mthumb -mcpu=cortex-m33 -march=armv8-m.main+fp+dsp -mcmse
  -mfloat-abi=softfp`, `.uf2` output, `RUN_FROM_RAM` option).
- `betaflight/src/platform/PICO/link/*.ld` — RP2350 linker scripts
  (Flash/RAM/Hybrid run modes + memory map).
- `betaflight/src/platform/PICO/target/{common/target_RP2350.h,RP2350A,RP2350B}`
  — thin target definitions.
- `betaflight/src/platform/STM32/mk/STM32H7.mk` as a cross-check — structurally
  near-identical in shape to Wingflight's own
  [make/mcu/STM32H7.mk](../make/mcu/STM32H7.mk), confirming the `RP2350.mk`
  variable conventions (`ARCH_FLAGS`/`DEVICE_FLAGS`/`MCU_COMMON_SRC`/`VCP_SRC`)
  translate fairly mechanically into a new `make/mcu/RP2350.mk`.

## Phase 0 — Decisions needed before coding starts

- [x] Target variants: **all four — RP2350A, RP2350B, RP2354A, RP2354B**
      (RP2354x has built-in on-die flash, RP2350x uses external QSPI flash;
      A/B differ only in package/GPIO count).
- [ ] Specific board / reference design to bring up first (drives which
      variant is validated on real hardware first; the other three follow
      from shared code once one variant boots)?
- [x] Vendor pico-sdk (+ tinyusb) as a **git submodule** (matches Betaflight
      convention) rather than a committed source snapshot. Wingflight will add
      its first `.gitmodules` file for this.
- [x] Scope: **keep USB-MSC and multicore support, drop the OSD framebuffer**
      from the first pass. Wingflight has no fixed-wing OSD requirement yet;
      USB-MSC (mass-storage config/blackbox access) and multicore (core-1
      offload) are wanted and stay in scope.
- [x] **Blackbox storage medium, decided 2026-08-26: a dedicated external
      SPI/QSPI flash chip and an SD card, not the on-die/QSPI firmware flash.**
      This replaces the "carve a `FLASH_BLACKBOX` region out of the same
      physical flash the firmware/config live on" option raised during the
      USB-MSC/multicore survey (twelfth iteration) - that approach is now
      explicitly out of scope, which sidesteps its two real problems for
      free: (1) it would have needed `flash_safe_execute()`-style multicore
      XIP-write synchronization (`config_streamer.c`'s existing PICO flash
      writer only disables interrupts, not safe once core 1 is active - see
      its own `// TODO: synchronise second core` at the time of writing), and
      (2) it would have been RP2350A/B-only in practice (RP2354A/B's 2MB
      on-die flash has no real headroom once firmware+config are accounted
      for). A separate flash chip / SD card avoids both: no XIP contention
      (a different bus entirely from code fetches) and no dependency on how
      much firmware flash a given RP2350/RP2354 variant happens to have.
      Implementation should follow Wingflight's existing STM32
      `flashVTable_t`/`flashDevice_t` abstraction (`drivers/flash_impl.h`,
      e.g. `flash_m25p16.c`/`flash_w25n.c` as reference drivers) for the SPI
      flash case, and the existing `USE_SDCARD`/`sdcard_spi.c` path for the
      SD card case, rather than a PICO-specific one-off - both should compose
      normally with `io/flashfs.c`/`blackbox/blackbox_io.c` once wired to a
      real PICO SPI bus. This decision also means USB-MSC (Phase 5) has a
      real backend to expose once either storage path is implemented, and
      multicore's blackbox-I/O-offload candidate (Phase 5's core-1 task
      consumer design) becomes concrete instead of speculative.

## Phase 1 — Toolchain feasibility spike

- [x] Confirm `tools/gcc-arm-none-eabi-9-2020-q2-update` builds
      `-mcpu=cortex-m33 -march=armv8-m.main+fp+dsp` code. **Confirmed
      2026-08-25**: GCC 9.3.1 (the existing toolchain, no upgrade needed)
      compiles this cleanly; `readelf -A` on the output object confirms
      `Tag_CPU_arch: v8-M.mainline`, `Tag_FP_arch: FPv5/FP-D16 for ARMv8`,
      `Tag_DSP_extension: Allowed`. Since Wingflight uses one shared
      `arm-none-eabi-gcc` toolchain for every target (root `Makefile` always
      invokes `$(ARM_SDK_PREFIX)gcc`), no separate/second toolchain is needed
      for RP2350 — this materially simplifies the port vs. betaflight's
      multi-toolchain-capable build.
- [x] Link a minimal blinky against a pared-down pico-sdk subset outside the
      main build to prove toolchain viability before investing in driver
      ports. **Superseded 2026-08-25**: went straight to wiring up the real
      build (Phase 2/3) instead of a separate throwaway blinky, since the
      toolchain check above already answered the key risk question. See
      Phase 3 below for the actual first real build attempt.

## Phase 2 — Build system plumbing

- [x] Add `.gitmodules` entry for pico-sdk under `lib/modules/pico-sdk`
      (matches Betaflight's path convention). **Done 2026-08-25**: added as a
      real git submodule (not just a `.gitmodules` stanza) pinned at pico-sdk
      release tag `2.3.0` (shallow clone; `master` HEAD == the `2.3.0` tag at
      the time of vendoring). `lib/modules/pico-sdk/lib/tinyusb` initialized
      as the nested submodule-of-submodule per upstream, commit
      `86ad6e56c1700e85f1c5678607a762cfe3aa2f47`. `LIB_MODULES_DIR` added to
      the root `Makefile`.
- [x] Create `make/mcu/RP2350.mk`. **Done 2026-08-25** — see
      [make/mcu/RP2350.mk](../make/mcu/RP2350.mk). Translated from
      betaflight's `RP2350.mk` into Wingflight's flat conventions
      (`ARCH_FLAGS`/`DEVICE_FLAGS`/`MCU_COMMON_SRC`/`VCP_SRC`/`MSC_SRC`,
      `MCU_EXCLUDES`). Shared by all four variants; per-variant flash
      size/defines pushed into `RP2350_UNIFIED/target.mk`.
- [x] Port linker scripts into `src/link/`. **Done 2026-08-25**: copied
      verbatim from `betaflight/src/platform/PICO/link/` —
      `pico_flash_mem_defaults.ld`, `pico_rp2350_memory.ld`,
      `pico_rp2350_RunFromFLASH.ld`, `pico_rp2350_RunFromHybrid.ld`,
      `pico_rp2350_RunFromRAM.ld`. These are reused as-is for RP2354A/RP2354B
      too — RP2354's on-die flash is memory-mapped at the same XIP window
      (`0x10000000`) as external QSPI flash on RP2350, so the RAM/peripheral
      memory map does not need a separate `_memory.ld` for RP2354 (only the
      `PICO_FLASH_*` defines differ, handled in `target.mk`, see the open
      TODO about on-die flash boot_stage2/QMI defaults below).
- [ ] Add a real `.uf2` post-build step (Wingflight currently only emits
      hex/bin/elf via `OBJCOPY`; betaflight's `DEFAULT_OUTPUT := uf2` implies
      a `picotool`/`elf2uf2`-based step). **Not started** — deferred until a
      target actually links successfully (see Phase 3 blocker below).
- [x] Register `RP2350A`, `RP2350B`, `RP2354A`, `RP2354B`. **Done 2026-08-25**
      — but using Wingflight's existing **unified-target ALT_TARGETS pattern**
      instead of four separate target folders like betaflight: one folder
      [src/main/target/RP2350_UNIFIED/](../src/main/target/RP2350_UNIFIED/)
      with a shared `target.h`/`target.mk`, four empty alt-marker files
      (`RP2350A.mk`, `RP2350B.mk`, `RP2354A.mk`, `RP2354B.mk`) and a
      `RP2350_UNIFIED.nomk` marker (mirrors
      [src/main/target/STM32_UNIFIED/](../src/main/target/STM32_UNIFIED/)
      exactly). `make/targets.mk` updated with a new `RP2350_TARGETS` group
      mapping to `TARGET_MCU := RP2350`.

## Phase 3 — Minimal bring-up target

- [ ] Clock init, GPIO, UART console, LED only.
- [ ] Validate boot / flash-via-uf2 before wiring in the full driver set.
- [x] Produce a first linked RP2350B image. **Done 2026-08-25**: after
      resolving build-system/source-list drift and adding PICO-safe guards for
      several shared STM32 assumptions, `make TARGET=RP2350B` now compiles and
      links `obj/wingflight_4.6.0_RP2350B.hex`. Hardware boot and UF2 output are
      still pending.
- [ ] Bring-up order: RP2350B first (straight port path, matches betaflight's
      primary variant), then RP2350A (same port path, smaller GPIO count),
      then derive RP2354B and RP2354A (on-die flash variants) once their
      RP2350 counterparts boot cleanly.

### First real build attempt (2026-08-25) — ported files + current blocker

Ran `make TARGET=RP2350B` against the Phase 2 scaffolding. The build system
wiring itself works end-to-end (target resolves, `TARGET_MCU := RP2350`,
correct files get compiled with the right optimisation flags) and produced
real, useful compiler errors rather than Makefile errors — i.e. Phase 2 is
solid and Phase 3/4 driver work is now the bottleneck, as expected.

**Files ported this session** (verbatim copy from `betaflight/src/platform/PICO/`
unless noted):

- Drivers → `src/main/drivers/`: `adc_pico.c`, `bus_i2c_pico.c`,
  `bus_quadspi_pico.c`, `bus_spi_pico.c`, `debug_pico.c`, `debug_pin.c`,
  `dma_pico.c`, `dshot_bidir_pico.c`, `dshot_pico.c` (+`.h`),
  `dshot_pio_programs.h`, `dshot.pio`, `exti_pico.c`, `gyro_clkin_pico.c`,
  `io_pico.c`, `light_ws2811strip_pico.c`, `memfunctions.S`, `multicore.c`,
  `pwm_beeper_pico.c`, `pwm_motor_pico.c`, `pwm_servo_pico.c`,
  `serial_usb_vcp_pico.c`, `config_flash.c`, `usbd_msc_mem.h`.
- `src/main/drivers/uart_pico/`: `serial_uart_pico.c` (+`.h`), `uart_hw.c`,
  `uart_pio.c`, `uart_rx_program.c`, `uart_tx_program.c` (new subfolder — the
  only PICO driver group kept as a subfolder rather than flattened, since
  `startup/` is the only precedent for MCU-specific subfolders in Wingflight).
- `src/main/drivers/usb_pico/`: `tusb_config.h`, `usb_cdc.c` (+`.h`),
  `usb_descriptors.c`, `usb_msc_pico.c`.
- **Renamed to avoid clobbering existing STM32 shared files with the same
  name** (both already existed in `src/main/drivers/` and are STM32-HAL
  specific — confirmed by inspection before copying): `system.c` →
  `system_rp2350.c`, `persistent.c` → `persistent_rp2350.c`. The generic
  filenames are added to `MCU_EXCLUDES` in `RP2350.mk` so they're never
  compiled for RP2350 targets, matching the existing
  `STM32H7.mk`/`MCU_EXCLUDES` precedent.
- `src/main/drivers/bus_i2c_utils.c` (+`.h`) — copied from
  `betaflight/src/main/drivers/` (the *common*, not PICO-specific, betaflight
  tree — this file didn't exist in Wingflight at all, needed by
  `bus_i2c_pico.c`).
- `src/main/drivers/rp2350_config/pico/{version.h,config_autogen.h}` — pico-sdk
  config stub headers (normally CMake-autogenerated; betaflight hand-wrote
  them for their Make-based build, same approach used here). `version.h`
  updated to report `2.3.0` (our pinned submodule tag) instead of
  betaflight's `2.1.0`. Kept in a dedicated `rp2350_config/` include dir so
  `#include "pico/version.h"` resolves without clashing with Wingflight's own
  unrelated `src/main/build/version.h`.
- Excluded entirely (per Phase 0 OSD-drop decision): `betaflight/src/platform/PICO/osd/*`.

**Shared-header changes** (small, isolated additions — existing STM32/other
family branches left untouched):

- [src/main/common/platform.h](../src/main/common/platform.h): added an
  `#elif defined(PICO)` branch to the chipset-detection chain (previously
  fell through to `#error "Invalid chipset specified"` for any
  non-STM32/non-SIMULATOR/non-UNIT_TEST build). `-DPICO` is defined globally
  for all four variants in `RP2350.mk`.
- [src/main/drivers/io.h](../src/main/drivers/io.h): added a `#elif
  defined(PICO)` branch for the `IOCFG_*` constants (parallel to the existing
  `UNIT_TEST`/`SIMULATOR_BUILD` stub branch, but with distinct in/out values
  since `io_pico.c`'s `IOConfigGPIO()` currently only distinguishes
  input(0)/output(1) — it has its own upstream `TODO` for finer-grained
  config); and guarded the STM32-only `GPIO_TypeDef* IO_GPIO(IO_t io);`
  declaration with `#if !defined(PICO)` since `io_pico.c` never implements or
  calls it (PICO driver code only ever uses `IO_Pin(io)`, a bare GPIO number).
- [src/main/drivers/bus_i2c.h](../src/main/drivers/bus_i2c.h): the
  `I2CDEV_COUNT` fallback `#else` branch unconditionally defined the macro
  (no `#ifndef` guard), clashing with the `I2CDEV_COUNT 2` already set in
  `RP2350_UNIFIED/target.h`. Added an `#elif defined(PICO)` no-op branch so
  the per-target value from `target.h` wins; STM32F4/STM32F7/default
  behavior unchanged.

**Current blocker (open, not yet fixed) — IO tag pin-width vs. RP2350 GPIO
count**: Wingflight's `ioTag_t` is `uint8_t`
([src/main/drivers/io_types.h](../src/main/drivers/io_types.h)) packed as
4 bits port-id + 4 bits pin (`DEFIO_TAG_MAKE` in
[src/main/drivers/io_def.h](../src/main/drivers/io_def.h)), i.e. max 16 pins
per "port". `io_pico.c` (as shipped by betaflight) models the whole RP2350 as
a **single virtual port** and explicitly `#error`s if
`DEFIO_PORT_USED_COUNT > 1` — this is an upstream betaflight limitation, not
something Wingflight introduced (confirmed: betaflight's own `io_types.h` also
uses `uint8_t ioTag_t`, unchanged from Wingflight's). With only 4 pin bits
available, a single virtual port tops out at 16 assignable GPIO resources,
which is likely too few for a real flight-controller board (motors, RX,
telemetry, LEDs, buttons, I2C/SPI bus pins, etc. easily exceed 16). Also still
outstanding: `io_def_generated.h`'s per-target pin table needs real
`TARGET_IO_PORTx`-equivalent definitions for RP2350 (currently produces an
empty/warning-as-error "No pins are defined" build failure since
`RP2350_UNIFIED/target.h` doesn't yet define any pins).

**Next step options for this blocker** (left as an open design decision for
the next session, not resolved yet):
1. Model RP2350's up to 48 GPIOs as **3 virtual 16-pin "ports"**
   (GPIO 0–15 / 16–31 / 32–47) reusing the existing 8-bit `ioTag_t` scheme
   unmodified (safest — zero risk to STM32 targets since the tag width
   doesn't change), which requires extending `io_pico.c`'s pin-decode logic
   to support `DEFIO_PORT_USED_COUNT > 1` (removing/replacing its current
   single-port `#error`), plus defining a per-board pin table in
   `RP2350_UNIFIED/target.h` (or a real per-board target once one is chosen —
   see the still-open Phase 0 "which board first" item).
2. Widen `ioTag_t`/`DEFIO_TAG_MAKE` globally — **not recommended**: touches
   every target (STM32 included) and any persisted/packed config or MSP
   layout that encodes `ioTag_t`, far higher risk for a change that only
   RP2350 needs.
### Second build iteration (2026-08-25, same day) \u2014 shared-header quick fixes + a much bigger discovery

After the fixes above, re-running `make TARGET=RP2350B` surfaced several more
**quick, safe, isolated** fixes (all applied, all `#elif defined(PICO)`/opaque
placeholder-type additions with zero effect on other families \u2014 verified by
rebuilding `STM32F405` clean after each batch):

- `make/mcu/RP2350.mk`: added the missing pico-sdk `pico_platform_common`
  component (include dir + `common.c` source + `-DLIB_PICO_PLATFORM_COMMON=1`)
  \u2014 `bus_i2c_pico.c`'s include chain needed `pico/platform/common.h`, which
  wasn't in `SYS_INCLUDE_DIRS` yet.
- `make/mcu/RP2350.mk`: pico-sdk's own headers use GNU/pedantic-flagged
  constructs (`hardware/dma.h`'s `invalid_params_if()` statement-expression
  macro, `pico/platform.h`'s directive-inside-macro-argument in
  `busy_wait_at_least_cycles()`) that the root Makefile's global `-Wpedantic
  -Werror` turns into hard errors (Betaflight's own build never enables
  `-Wpedantic` at all, so this never came up upstream). Fixed with
  `EXTRA_FLAGS += -Wno-pedantic` in `RP2350.mk` \u2014 **note**: the Makefile's own
  `TEMPORARY_FLAGS` hook looks like the obvious place for this but doesn't
  work, because root `Makefile` does an unconditional `TEMPORARY_FLAGS :=`
  reset *after* `make/mcu/<FAMILY>.mk` is included; `EXTRA_FLAGS` is the
  correct hook (appended last in `CFLAGS`, never reset).
- `make/mcu/RP2350.mk`: removed the ported `drivers/debug_pin.c` from
  `MCU_COMMON_SRC` \u2014 wingflight already has a generic, always-compiled
  `src/main/build/debug_pin.c` (gated behind `USE_DEBUG_PIN`, which no target
  defines by default) that isn't present at all in the vendored betaflight
  snapshot; having both would define duplicate `dbgPinInit`/`dbgPinHi`/
  `dbgPinLo` symbols and fail to link. The unused ported copy is still on
  disk at `src/main/drivers/debug_pin.c` but no longer referenced by the
  build; can be deleted in a later cleanup pass.
- `src/main/common/platform.h`: the PICO branch now also declares a small set
  of **opaque placeholder types** for STM32-shaped fields/signatures used
  unconditionally in wingflight's shared driver bookkeeping headers
  (`io_impl.h`, `bus_i2c_impl.h`, `bus.h`, `dma.h`, `adc.h`) that PICO driver
  code never dereferences: `GPIO_TypeDef`, `I2C_TypeDef`, `ADC_TypeDef`,
  `DMA_TypeDef`, `SPI_TypeDef` (all `typedef void ...;`, pointer-only usage),
  `IRQn_Type` (`typedef int32_t ...;`, pico-sdk's own API uses plain ints for
  IRQ numbers, not CMSIS's enum), and `DMA_InitTypeDef` (a real 1-member
  placeholder struct, since `drivers/bus.h`'s `extDevice_s` embeds it
  **by value**, so it can't be an incomplete/opaque type). **Important
  false start, documented so it isn't retried**: initially tried
  `#include "RP2350.h"` (the pico-sdk CMSIS device header) to get a real
  `IRQn_Type` \u2014 this actively **breaks the build**, because `RP2350.h`
  `#define`s the same peripheral base-address macros (`SIO_BASE`,
  `PPB_BASE`, `EPPB_BASE`, ...) that pico-sdk's own
  `hardware/regs/addressmap.h` also defines, and the two conflict
  (redefinition errors) the moment both end up in one translation unit via
  `hardware/i2c.h`/etc. Use the plain-int opaque typedef instead; do not
  include pico-sdk's/CMSIS's device header from `platform.h`.
- `src/main/drivers/bus_i2c.h`: (already covered above) `I2CDEV_COUNT` guard.

**Bigger discovery**: with the above fixed, the remaining errors are no
longer shared-header gaps \u2014 they're **missing files**, specifically
betaflight's newer, more-abstracted per-platform headers that several ported
`*_pico.c` drivers `#include` directly and which simply don't exist anywhere
in wingflight:

- `adc_pico.c` \u2192 `#include "platform/dma.h"` (not found)
- `bus_i2c_pico.c` \u2192 `#include "platform/platform.h"` (not found)
- `bus_spi_pico.c` \u2192 `#include "drivers/bus_spi_types.h"` (not found)

These are betaflight's `src/platform/<NAME>/include/platform/*.h` files (a
whole per-platform header layer that doesn't exist in wingflight's older flat
layout at all \u2014 io_pico.c happens not to need this layer, which is why it
compiled with only the smaller io.h-family fixes above, but adc_pico.c,
bus_i2c_pico.c and bus_spi_pico.c depend on it directly). This confirms, with
concrete file-level evidence, the risk flagged back in Phase 0/"Major
mismatches" #2: several ported driver files are **not drop-in copies** and
need either (a) a purpose-built, wingflight-side compatibility header
providing just the subset of `platform/dma.h` / `platform/platform.h` /
`drivers/bus_spi_types.h` that these specific files actually use, or (b)
per-file rewriting against wingflight's existing `dma.h`/`bus.h`/`bus_spi*`
APIs directly (more invasive, more correct long-term, no shim layer to
maintain). **Not started** \u2014 needs a deliberate design pass (read what each
of those three missing headers is expected to provide, by reading betaflight's
`src/platform/PICO/include/platform/*.h` and comparing field-by-field against
wingflight's `dma.h`/`bus.h`), not ad-hoc empty-header stubbing.
## Phase 4 — Core drivers

- [ ] DMA
- [ ] Timer / PIO plumbing
- [ ] Motor / PWM (DSHOT via PIO)
- [ ] ADC
- [ ] I2C
- [ ] SPI
- [ ] Persistent config-in-flash — two flash strategies to support across the
      four targets: external QSPI flash (RP2350A/RP2350B, ported from
      betaflight) and on-die flash (RP2354A/RP2354B, new work).

Each driver must be checked against Wingflight's existing driver headers
rather than assumed to be a drop-in copy from `betaflight/src/platform/PICO/`.
Explicitly **not** porting `betaflight/src/platform/PICO/osd/*` (OSD
framebuffer support dropped from scope per Phase 0 decision).

## Phase 5 — USB VCP + USB-MSC + multicore (TinyUSB)

- [ ] Implement CLI/MSP over TinyUSB-based serial (VCP). This is net-new —
      Wingflight's existing `vcp`/`vcpf4`/`vcp_hal` are STM32 USB-OTG specific
      and don't apply.
- [ ] Port USB-MSC support (`PICO/usb/usb_msc_pico.c`,
      `drivers/usb_msc_common.c`, `msc/usbd_storage*.c`, `msc/emfat*.c`) so the
      flight controller can expose blackbox storage as a mass-storage
      device over USB. **Deferred 2026-08-25**: temporarily disabled for the
      first RP2350B link because Wingflight's existing `msc/usbd_storage*.c`
      pulls STM32 USB-device headers (`usbd_msc_core.h` -> `usbd_conf.h`) rather
      than TinyUSB-native types. Needs a deliberate TinyUSB storage adapter.
      **Update 2026-08-26**: re-surveyed in the twelfth iteration -
      `drivers/usb_pico/usb_msc_pico.c` turned out to already *be* that
      TinyUSB adapter (implements every `tud_msc_*` callback against the
      existing generic `USBD_STORAGE_cb_TypeDef` fops struct, no new
      abstraction needed) - it's just not in `RP2350.mk`'s source list
      (`MSC_SRC :=` is still empty) because there's no storage backend for it
      to expose yet. Per the storage-medium decision above (external SPI
      flash + SD card, not firmware flash), the real remaining work is that
      backend, not the MSC adapter layer itself - once a `flashVTable_t`
      SPI-flash driver or `sdcard_spi.c` is wired up for a real PICO SPI bus,
      re-enabling `MSC_SRC` should be close to mechanical.
- [ ] Port multicore support in two parts:
      1. **Straight port** (low risk): `platform/multicore.h`, `multicore.c`
         (queue-based `multicoreExecute()`/`multicoreExecuteBlocking()` RPC to
         core 1), and the `DMA_IRQ_CORE_NUM 1` wiring in the ported `dma.c` so
         all DMA-completion interrupts land on core 1, keeping core 0 (the
         scheduler/PID loop) free of DMA ISR jitter. Depends only on
         pico-sdk's `pico_multicore`/`pico_sync`/`pico_util`, already in the
         vendored SDK source list — no new dependency needed.
      2. **New work** (not present upstream — Betaflight's `core1_main` is
         just a command consumer with a `// TODO call scheduler here for
         core 1 tasks`): build a real lightweight task consumer on core 1 and
         move latency-tolerant, non-deterministic-timing work onto it —
         candidates: USB-MSC block read/write servicing, blackbox I/O
         flushing, CLI/MSP serial parsing outside the hot path, DSHOT
         telemetry decode. Keep gyro sampling → filtering → PID → mixer →
         motor output entirely on core 0 — that chain is serially
         data-dependent and must not be split across cores.
      - [ ] Decide whether to also enable `ENABLE_MULTICORE_INIT` (running FC
            init phases 1/2 on core 1 during boot, per `target_RP2350.h`) —
            boot-time only, not a runtime performance factor.

### Core-1 task consumer design (new work, detailed)

Goal: core 0 stays a single-threaded, deterministic flight-control loop
exactly like the STM32 targets today; core 1 is a best-effort helper that
never blocks or is blocked on by core 0's control path.

- **Boot/launch**: core 1 is started once during `systemInit()` (same point
  Betaflight launches it via `multicore_launch_core1`), before the scheduler
  starts on core 0. Stack size fixed via `PICO_CORE1_STACK_SIZE` (already
  `0x1000` in `RP2350.mk`'s `DEVICE_FLAGS`). Core 1's `core1_main()` sets up
  its own ring buffers/queues, then enters an infinite best-effort loop —
  it never returns except on `MULTICORE_CMD_STOP`.
- **Two independent transport mechanisms, not one**:
  1. *Command RPC* — reuse Betaflight's existing `queue_t` pair
     (`core0_queue`/`core1_queue`) and `multicoreExecute()` /
     `multicoreExecuteBlocking()` API verbatim, for occasional one-shot calls
     (e.g. `ENABLE_MULTICORE_INIT`'s init-phase dispatch). `queue_t` has an
     internal spinlock and can block on a full queue — acceptable only for
     rare, non-hot-path calls, **never** for per-loop-iteration data.
  2. *Work buffers* — new, per-consumer **lock-free single-producer /
     single-consumer ring buffers** (one each for USB-MSC block I/O,
     blackbox flush chunks, CLI/MSP RX bytes) rather than routing recurring
     high-frequency data through the shared `queue_t`. Core 0 writes without
     ever blocking (drops/backpressures if full — never stalls the control
     loop); core 1 drains them in its round-robin loop. This avoids
     reintroducing the exact jitter multicore is meant to remove.
- **Core-1 main loop shape**: a simple non-blocking round-robin, no RTOS and
  no reuse of [scheduler.c](../src/main/scheduler/scheduler.c) (that
  scheduler is built around core-0-specific cycle counting and is not
  meant to run twice) — each pass: (1) service DMA-IRQ-fed data already
  affinitized to core 1 via `DMA_IRQ_CORE_NUM`, (2) drain one command from
  the RPC queue if present, (3) pump a bounded chunk of each work-buffer
  consumer (USB-MSC, blackbox, CLI) so no single item can hog core 1
  indefinitely, (4) `tight_loop_contents()`/WFE to idle.
- **ISR-context rule**: any interrupt handler affinitized to core 1 (DMA
  completion today) must stay short and non-blocking — push data into a
  ring buffer and return; the actual work (e.g. copying a completed DMA
  buffer into the blackbox stream) happens in the core-1 main loop, not the
  ISR, to keep interrupt latency bounded.
- **Failure isolation**: core 0 must never depend on a core-1 result for
  flight safety. A simple heartbeat counter incremented by core 1 and
  checked periodically by a core-0 task can detect a wedged core 1 (e.g. USB
  host issue) and degrade gracefully (disable MSC/blackbox reporting) without
  affecting flight control.
- **Config surface**: gated behind `USE_MULTICORE` (as upstream) plus new
  Wingflight-specific feature flags for which consumers run on core 1
  (e.g. enable USB-MSC-on-core1 and blackbox-on-core1 independently), so a
  target can opt out of the new work-buffer scheme and fall back to
  Betaflight's plain RPC-only behavior if something regresses.

### Isolation requirement: must not affect existing STM32 (single-core) targets

Hard constraint — multicore work is additive-only for RP2350/RP2354 and must
be invisible to STM32F4/F7/G4/H7 builds:

- [ ] All new/ported files (`multicore.c`, ring-buffer consumers, core-1 main
      loop) live only under the RP2350-family driver set, added via
      `make/mcu/RP2350.mk`'s `MCU_COMMON_SRC` — never added to
      [make/source.mk](../make/source.mk)'s `COMMON_SRC`, which is compiled
      for every target including STM32. Same pattern already used for
      MCU-specific files like `adc_stm32h7xx.c`.
- [ ] `USE_MULTICORE` (and any new Wingflight offload flags) is defined only
      in RP2350/RP2354 `target.h` files, never in shared headers
      (`common_pre.h`, `common_defaults_post.h`) or STM32 target files.
- [ ] `multicoreExecute()`/`multicoreExecuteBlocking()` keep Betaflight's
      existing `#else` fallback (direct synchronous function call when
      `USE_MULTICORE` is undefined) — if any shared (non-driver) code ever
      calls these, STM32 builds must execute identically to today, just
      without the dispatch.
- [ ] **No changes to** [scheduler.c](../src/main/scheduler/scheduler.c),
      [fc/tasks.c](../src/main/fc/tasks.c), or [fc/init.c](../src/main/fc/init.c)
      to add cross-core awareness — the scheduler stays core-0/single-core
      semantics everywhere. Core-1 offload is implemented as an alternate
      *driver-level* transport under blackbox/CLI/MSC (e.g. swapping which
      function flushes a buffer), not as branching added to the shared logic
      files those subsystems already use on STM32.
- [ ] Verification: after implementing, do a clean build of at least one
      existing STM32 target (e.g. `STM32F405`) and confirm it builds
      successfully with no changes in generated object list, to catch any
      accidental coupling into shared code paths.

### Multicore vs. STM32F405/STM32F722 performance (reference notes, 2026-08-25)

| | Core | Clock (official) | Pipeline | FPU |
|---|---|---|---|---|
| STM32F405 | Cortex-M4 | 168 MHz | single-issue, 3-stage | single-precision |
| STM32F722 | Cortex-M7 | 216 MHz | dual-issue superscalar, 6-stage, branch prediction, TCM/cache | single-precision |
| RP2350 | Cortex-M33 ×2 | 150 MHz (datasheet spec) | single-issue, similar depth to M4 | single-precision (present on RP2350) |

- A single RP2350 core is roughly F405-class per clock (slightly under, since
  150 MHz < 168 MHz and M33/M4 IPC is similar for typical filter/PID C code).
  It will **not** match F722's per-core throughput — M7's dual-issue pipeline
  gives it a real IPC advantage on top of a higher clock.
- The second core does not speed up the sequential gyro→PID→mixer→motor path
  (inherently single-threaded, must stay that way for lowest latency). Its
  value is removing DMA-ISR jitter and non-deterministic housekeeping
  (USB-MSC, blackbox, CLI/MSP) from the core running the scheduler — which a
  single-core F405/F722 cannot do. This can make loop-time **consistency**
  competitive with or better than a heavily-loaded F405, even though the raw
  core clock is lower.
- Community RP2350 overclocking (200–300+ MHz) exists but is
  unofficial/unvalidated for voltage/thermal margin — not assumed by this
  plan; a candidate future research item only.
- Practical takeaway: treat RP2350 as roughly F405-class parity target
  (helped by multicore jitter reduction), not an F722 replacement for raw
  single-thread filter/PID throughput.

## Phase 6 — Validation

- [ ] RX/telemetry serial protocols validation (mostly MCU-agnostic already).
- [ ] Mixer/servo validation on real fixed-wing hardware.
- [ ] Update [Changes.md](../Changes.md) / [README.md](../README.md) once RP2350
      support is functional.

## Progress Log

- 2026-08-24: Initial research completed and plan written. No implementation
  started yet. Phase 0 decisions still pending user input.
- 2026-08-24: Phase 0 decisions made: use git submodules for pico-sdk/tinyusb
  (new `.gitmodules` for Wingflight); target both RP2350B and RP2354B
  (RP2354B has built-in/on-die flash, not present in upstream Betaflight —
  will be derived from the ported RP2350B target). No implementation started
  yet.
- 2026-08-24: Scope expanded to all four variants — RP2350A, RP2350B,
  RP2354A, RP2354B. RP2350A/B are straight ports from betaflight; RP2354A/B
  are new work derived from their RP2350 counterparts (on-die flash instead
  of external QSPI flash). No implementation started yet.
- 2026-08-24: Phase 0 scope decision: keep USB-MSC and multicore support in
  scope, drop the OSD framebuffer entirely (not needed for fixed-wing use
  case). Phase 5 renamed/expanded to cover VCP + USB-MSC + multicore. No
  implementation started yet.
- 2026-08-25: Detailed the multicore implementation approach (straight port
  of `multicore.c` + `DMA_IRQ_CORE_NUM` DMA-ISR offload, plus new work to
  build a real core-1 task consumer for USB-MSC/blackbox/CLI offload) and
  added performance-comparison notes vs. STM32F405/F722 to Phase 5. No
  implementation started yet.
- 2026-08-25: Expanded the core-1 task consumer into a full design (boot
  sequence, dual transport: RPC queue for one-shot calls + new lock-free
  per-consumer ring buffers for recurring data, non-blocking round-robin
  main loop, ISR-context rule, heartbeat-based failure isolation, per-target
  config flags). No implementation started yet.
- 2026-08-25: Added an explicit isolation requirement: multicore work must be
  additive-only and invisible to existing STM32 targets (MCU_COMMON_SRC-only
  placement, RP2350-only flags, preserved non-multicore fallback path, no
  changes to scheduler.c/fc/tasks.c/fc/init.c, STM32 clean-build verification
  step). No implementation started yet.
- 2026-08-25: **Implementation started.** Phase 1 confirmed (GCC 9.3.1 already
  supports cortex-m33/armv8-m.main+fp+dsp, no toolchain change needed). Phase 2
  completed: added pico-sdk (+tinyusb) as real git submodules pinned at
  `2.3.0`, wrote `make/mcu/RP2350.mk`, ported the 5 RP2350 linker scripts into
  `src/link/`, and registered all four variants (`RP2350A`/`RP2350B`/
  `RP2354A`/`RP2354B`) via a new `src/main/target/RP2350_UNIFIED/` following
  Wingflight's existing unified-target ALT_TARGETS pattern (mirrors
  `STM32_UNIFIED/`) rather than betaflight's four-separate-folders layout.
  Ported ~30 non-OSD driver files from `betaflight/src/platform/PICO/` into
  `src/main/drivers/` (renaming the two that collided with existing STM32
  files: `system.c`→`system_rp2350.c`, `persistent.c`→`persistent_rp2350.c`,
  excluded via `MCU_EXCLUDES`). Ran a first real `make TARGET=RP2350B` build:
  fixed three small, isolated shared-header gaps (`platform.h` chipset
  branch, `io.h` IOCFG_*/GPIO_TypeDef guard, `bus_i2c.h` I2CDEV_COUNT clash),
  all additive `#elif`/`#if !defined(PICO)` guards with zero change to
  existing STM32 branches. Hit a real, precisely-identified architectural
  blocker: Wingflight's 8-bit `ioTag_t` (4-bit port + 4-bit pin, max 16 pins
  per port) vs. betaflight's `io_pico.c` single-virtual-port assumption caps
  usable GPIO resources at 16 for RP2350, likely too few for a real board;
  see Phase 3 for the two solution options identified (favoring the 3x16-pin
  virtual-port approach, zero risk to STM32 tag width). Not yet resolved -
  next session should start there.
- 2026-08-25: Continued iterating on the same build. Fixed several more
  isolated shared-header/build-flag gaps (missing pico-sdk `pico_platform_common`
  component, `-Wpedantic` false positives from pico-sdk's own headers via a
  new `EXTRA_FLAGS` hook in RP2350.mk - noting `TEMPORARY_FLAGS` looks right
  but doesn't work, since the root Makefile resets it after mcu-file inclusion
  - a `debug_pin.c` duplicate-symbol landmine (wingflight already has a
  generic, always-built `build/debug_pin.c` not present upstream in
  betaflight; removed the ported PICO copy from MCU_COMMON_SRC), and rounded
  out `platform.h`'s PICO branch with opaque placeholder types
  (GPIO_TypeDef/I2C_TypeDef/ADC_TypeDef/DMA_TypeDef/SPI_TypeDef/IRQn_Type/
  DMA_InitTypeDef) for STM32-shaped fields that PICO driver code never
  dereferences. Documented a false start (including pico-sdk's CMSIS
  `RP2350.h` device header conflicts with pico-sdk's own
  `hardware/regs/addressmap.h` - don't do that). Verified STM32F405 still
  builds and links cleanly (full hex + memory report) after each round of
  shared-header edits, confirming isolation. With those fixed, hit a bigger,
  more precise discovery: `adc_pico.c`, `bus_i2c_pico.c`, and `bus_spi_pico.c`
  each `#include` a betaflight-newer-platform-abstraction header
  (`platform/dma.h`, `platform/platform.h`, `drivers/bus_spi_types.h`
  respectively) that doesn't exist anywhere in wingflight - confirms Phase
  0's "Major mismatch #2" risk with concrete file-level evidence. This
  (not the IO-tag/pin-width issue) is now the most significant remaining
  Phase 3/4 blocker and needs a deliberate design pass, not ad-hoc stubbing.
- 2026-08-25: Solved the `RP2350.h`/`addressmap.h` false-start mystery:
  betaflight's own `RP2350.mk` routes all pico-sdk/CMSIS include dirs through
  `-isystem` (not `-I`) specifically to work around
  [pico-sdk#2451](https://github.com/raspberrypi/pico-sdk/issues/2451) -
  GCC tolerates benign macro redefinitions in system-header-reached files.
  Wingflight's root `Makefile` had no `-isystem` plumbing at all; added
  `$(addprefix -isystem,$(SYS_INCLUDE_DIRS))` to `CFLAGS`/`ASFLAGS` (additive,
  `SYS_INCLUDE_DIRS` defaults empty elsewhere) and stopped folding
  `SYS_INCLUDE_DIRS` into plain `INCLUDE_DIRS` in `RP2350.mk`. Then wrote three
  new compat files to unblock the `platform/*.h` blocker above: 
  `src/main/drivers/bus_spi_types.h` (verbatim from betaflight, trivial opaque
  types), `src/main/platform/dma.h` (CH-handler macros only; the actual
  `DEFINE_DMA_CHANNEL`/`dmaIdentifier_e`/`DMA_FIRST_HANDLER` content was added
  as a new `#elif defined(PICO)` branch directly in wingflight's existing
  `src/main/drivers/dma.h`, matching its established per-MCU-family pattern -
  had to hand-adapt field names since wingflight's older
  `dmaChannelDescriptor_t` shape differs from betaflight's newer one), and
  `src/main/platform/platform.h` (thin shim - the real content, including new
  `I2C_INST()`/`SPI_INST()`/`UART_INST()` macros, was centralized in
  `src/main/common/platform.h`'s PICO branch since `bus_spi_pico.c` uses
  `SPI_INST()` without ever including `platform/platform.h` at all). Verified
  `adc_pico.c`/`bus_i2c_pico.c` compile past this blocker; STM32F405 re-verified
  clean. This surfaced an even deeper blocker in `bus_spi_pico.c`: it depends
  on betaflight's newer **segment-based SPI DMA bus driver** (`busSegment_t`
  transfer chains, `dmaInitTx`/`dmaInitRx` pointer fields, `spiProcessSegmentsDMA`/
  `spiProcessSegmentsPolled`) which wingflight's shared (non-PICO)
  `bus_spi.c`/`bus.h`/`bus_spi_impl.h` (Betaflight-4.3-era, no segment
  chaining) doesn't have. Porting that whole newer architecture in would be
  an invasive change to shared SPI-bus code used by every STM32 target -
  conflicts with the project's "stay merge-friendly, avoid invasive
  refactors" priority. The likely right answer is a new, simpler
  `bus_spi_pico.c` written directly against wingflight's existing
  (non-segmented) SPI bus API instead of porting betaflight's file verbatim -
  flagged for user confirmation before starting, since it's real new driver
  code rather than a mechanical port.
- 2026-08-25: User confirmed the "write it against Wingflight's own SPI bus
  API" direction. Closer inspection showed Wingflight's `bus_spi.c`/`bus.h`
  actually ALREADY has a segment-chained DMA architecture very close in shape
  to betaflight's (just different field/function names and no separate
  "process segments" helper - that logic is inlined in `spiSequenceStart`),
  so the real fix was a surgical adaptation rather than a from-scratch
  rewrite. Using `bus_spi_ll.c` (STM32 LL) as the exact reference template:
  - Guarded the shared/generic `spiInitBusDMA` (and its now-unused
    `spiRxIrqHandler`/`spiTxIrqHandler` helpers) in `bus_spi.c` with
    `#if !defined(PICO)`, since it's built around STM32-only DMAMUX
    resource-map APIs and a `.stream` struct field that doesn't exist for
    PICO - `bus_spi_pico.c`'s own PICO-specific `spiInitBusDMA` is now the
    sole definition for PICO builds.
  - Made `common/platform.h`'s `DMA_InitTypeDef` a real alias for pico-sdk's
    `dma_channel_config` (was a dummy placeholder struct), matching
    betaflight's own approach - fixed a follow-on `MIN`/`MAX` redefinition
    clash this exposed (pico-sdk's headers define these too) by guarding
    Wingflight's own `common/maths.h` `MIN`/`MAX` macros with `#ifndef`
    (betaflight's fork already has this guard; Wingflight's had dropped it).
  - Rewrote `bus_spi_pico.c`'s DMA/segment internals to match Wingflight's
    real API: `initTx`/`initRx` field names (not `dmaInitTx`/`dmaInitRx`),
    `spiInternalInitStream(dev, bool preInit)` signature, inlined
    `spiSequenceStart`'s DMA/polled dispatch directly (removing calls to
    betaflight-only `spiProcessSegmentsDMA`/`spiProcessSegmentsPolled`),
    implemented `spiInternalResetDescriptors`/`spiInternalResetStream` for
    real, and exported `spiIrqHandler` (was `static` and had no header
    declaration) so `bus_spi_pico.c` can hand off DMA-completion handling to
    it, matching how betaflight's architecture does this.
  - Fixed several betaflight-vs-Wingflight naming mismatches in
    `bus_spi_pico.c`: `spiDevice_e`→`SPIDevice`, `OWNER_SPI_SDO`/`SDI`→
    `OWNER_SPI_MOSI`/`MISO`.
  - Added `src/main/drivers/pico_trace.h` (copied verbatim from betaflight) -
    the `bprintf(...)` debug-trace macro used across ~80 call sites in the
    ported `*_pico.c` files had no equivalent in Wingflight; it's a no-op
    unless `PICO_TRACE` is defined, so this is zero-cost by default.
  - Result: `bus_spi_pico.c`/`bus_spi.c` now compile with no errors of their
    own - the SPI-bus architecture blocker is fully resolved. The only
     remaining errors touching these files are cascades from the still-open
     IO-tag/pin-definition blocker (Phase 3, see above). STM32F405 re-verified
     clean/isolated after every change in this round.

### Fifth build iteration — remaining core drivers (config storage, motor PWM,
    multicore, misc) fixed; error count driven from 7 down to 4, new UART
    architecture blocker found

- Fixed (all build-verified, error count dropped each round: 7 -> 1 -> 4 -> 1
  -> 0-for-these-files -> 4 new ones in the UART subsystem):
  - `adc_pico.c`: renamed its private DMA ring-buffer array `adcValues` ->
    `picoAdcValues` (4 occurrences) - it collided with `adc_impl.h`'s shared
    `extern adcValues[ADC_CHANNEL_COUNT]`.
  - `bus_i2c_pico.c`: added missing `#include "hardware/gpio.h"` (every other
    ported `*_pico.c` file already had it).
  - `common/utils.h`: added `popcount`/`popcount32`/`popcount64` (ported
    verbatim from betaflight, 3-line `__builtin_popcount(ll)` wrappers) -
    Wingflight's frozen utils.h never had these; `adc_pico.c` calls `popcount`.
  - Config storage (`config_flash.c`'s betaflight-newer
    `configWriteWord()`/`config_streamer_buffer_type_t` API does not exist in
    Wingflight - Wingflight inlines all per-MCU-family flash erase/program
    logic directly in `config_streamer.c`'s own `write_word()` via
    `#elif defined(STM32Hx)/...` branches, with the buffer-align typedef named
    `config_streamer_buffer_align_type_t`). Fix: removed `config_flash.c` from
    `make/mcu/RP2350.mk`'s `MCU_COMMON_SRC` entirely (file left on disk,
    unused) and instead added a new `#elif defined(CONFIG_IN_FLASH) &&
    defined(PICO)` branch directly inside `config_streamer.c`'s `write_word()`
    (flash_range_erase/flash_range_program logic, adapted from the removed
    file), plus PICO no-op branches for the unlock/clear-flags/lock steps and
    an `|| defined(PICO)` addition to skip the STM32-only
    `getFLASHSectorForEEPROM()` helper entirely. Also fixed a **latent, always-
    dead bug** in `RP2350_UNIFIED/target.h`: `FLASH_CONFIG_STREAMER_BUFFER_SIZE`
    referenced pico-sdk's `FLASH_PAGE_SIZE` macro before `hardware/flash.h` was
    ever included at that point in target.h's parse - replaced with the
    literal `256`. Wired this macro into `config_streamer.h`'s buffer-size
    `#if` chain via a new top-priority `#if defined(FLASH_CONFIG_STREAMER_BUFFER_SIZE)`
    branch (so PICO gets a 256-byte, page-aligned config-write buffer instead
    of falling through to the generic 4-byte default).
  - `dma.h`/`dma_pico.c`: `DMA_IRQ_0_IRQn`/`DMA_IRQ_1_IRQn` (CMSIS-style,
    don't exist since Wingflight excludes RP2350.h) -> pico-sdk's own plain
    numeric `DMA_IRQ_0`/`DMA_IRQ_1` macros. Added missing
    `DMA_OUTPUT_INDEX`/`DMA_OUTPUT_STRING`/`DMA_INPUT_STRING` macros to
    `dma.h`'s existing PICO branch (used by `dmaGetDisplayString()`, matches
    betaflight's own PICO values).
  - **Motor PWM (`pwm_motor_pico.c`) - full rewrite**, same class of problem
    as bus_spi_pico.c/config storage: betaflight's ported file targets a
    newer motor-device API Wingflight doesn't have (`motorPwmDevInit(motorDevice_t*,
    const motorDevConfig_t*, uint16_t idlePulse) -> bool`, a `pwmMotors[]`
    global, `motorConfig->motorProtocol`/`MOTOR_PROTOCOL_*`/
    `useContinuousUpdate`/`motorOutputReordering[]`, and extra `motorVTable_t`
    fields Wingflight's `motor.h` doesn't have:
    convertExternalToMotor/convertMotorToExternal/decodeTelemetry/
    requestTelemetry/isMotorIdle/getMotorIO). Rewrote the whole file against
    Wingflight's real API (`motorPwmDevInit(const motorDevConfig_t*, uint8_t
    motorCount) -> motorDevice_t*`, global `motors[]` (not `pwmMotors[]`,
    matches `pwm_output.h`'s existing `extern` decl), `motorDevConfig->
    motorPwmProtocol`/`PWM_TYPE_ONESHOT125|ONESHOT42|MULTISHOT|STANDARD`/
    `useUnsyncedPwm`, direct `motorIndex` (no reordering field exists), a
    `write(uint8_t index, uint8_t mode, float value)` vtable signature with a
    `pwmConvertToInternal()` helper adapted from STM32 `pwm_output.c`, and
    locally-defined `pwmEnableMotors`/`pwmIsMotorEnabled`/`pwmGetMotors` since
    the STM32 versions in `pwm_output.c` are now excluded for PICO). All PICO
    PWM-slice hardware logic (clkdiv/wrap calc, `pwmShutdownPulsesForAllMotors`,
    one-shot `pwmCompleteOneshotMotorUpdate` latch-on-stop) preserved from the
    original port. Needed a new small shim `src/main/platform/pwm.h`
    (`picoPwmOutput_t` struct, ported verbatim from betaflight's
    `platform/PICO/include/platform/pwm.h`).
  - `pwm_output.c` (the STM32 motor PWM driver): guarded its entire body with
    `#if defined(USE_PWM_OUTPUT) && !defined(PICO)` (was just `#ifdef
    USE_PWM_OUTPUT`) so it doesn't duplicate-symbol-conflict with the
    rewritten `pwm_motor_pico.c`'s definitions of `motors[]`/
    `pwmEnableMotors`/`pwmIsMotorEnabled`/`motorPwmDevInit`/`pwmGetMotors` for
    PICO builds. Mirrors the `io.c`/`adc.c` PICO-exclusion pattern from
    earlier rounds.
  - `pwm_servo_pico.c` **removed from the build (architecturally
    incompatible, not fixable by renaming)**: it implements betaflight's
    newer `servoDevInit`/`servoWrite`/`servoDevConfig_t` API, which - unlike
    every other mismatch found so far - **does not exist ANYWHERE in
    Wingflight's actual source tree** (confirmed via exhaustive grep).
    Wingflight's real servo output is a completely different mechanism:
    `flight/servos.c` drives PWM directly via the STM32-timer `pwmOutConfig()`/
    `timerChannel_t`/`*ccr` API (the *same* low-level function motors use on
    STM32), with no separate device-driver abstraction at all. Removed
    `drivers/pwm_servo_pico.c` from `make/mcu/RP2350.mk`'s `MCU_COMMON_SRC`
    (file left on disk, unused). **Follow-up task, not started**: PICO servo
    PWM output needs a `#if defined(PICO)` branch written directly inside
    `flight/servos.c` itself (reusing `picoPwmOutput_t`/PWM-slice concepts
    from the now-rewritten `pwm_motor_pico.c`), not a drop-in servo device
    driver file.
  - `src/main/platform/multicore.h` (new shim, ported verbatim from
    betaflight's `platform/PICO/include/platform/multicore.h`): was missing
    entirely - `drivers/multicore.c` (already ported/present in Wingflight
    from an earlier round) and `drivers/system_rp2350.c` both
    `#include "platform/multicore.h"`.
  - `SystemCoreClock`: declared but never had an `extern` visible outside
    `system_rp2350.c` (which defines the real global and updates it from
    `clock_get_hz(clk_sys)`) - STM32 targets get this declaration
    transitively via their CMSIS `system_stm32*.h` startup headers, which
    PICO has no equivalent of. Added `extern uint32_t SystemCoreClock;` to
    `common/platform.h`'s PICO branch (needed by `pwm_motor_pico.c`'s clkdiv
    calculation, and already silently relied upon un-declared by
    `pwm_beeper_pico.c`).
  - STM32F405 re-verified clean (0 errors, full hex + memory report) after
    all of the above - isolation holds.
- **NEW BLOCKER FOUND (next up, not yet started)**: the UART subsystem
  (`drivers/uart_pico/serial_uart_pico.c` + siblings `uart_hw.c`, `uart_pio.c`,
  `uart_rx_program.c`, `uart_tx_program.c`, `serial_uart_pico.h`) is written
  against a betaflight-newer, PICO-specific **PIO-software-UART** architecture
  that doesn't exist in Wingflight at all:
  - `#include "drivers/serial_impl.h"` - a genuinely new, platform-agnostic
    shared header betaflight added (declares helpers like
    `serialOwnerTxRx()`/`serialOwnerIndex()`/`serialOptions_pull()`/
    `serialOptions_pushPull()`) - does not exist anywhere in Wingflight.
  - `#include "drivers/serial_uart_impl.h"` - also does not exist.
  - `serial_uart.h`'s `uartPort_t.USARTx` field is typed `USART_TypeDef *`,
    which (like `GPIO_TypeDef`/`SPI_TypeDef`/etc in earlier rounds) has no
    PICO placeholder typedef yet in `common/platform.h`.
  - The file also assumes a `SERIALTYPE_PIOUART` port type and a whole
    `_hw`/`_pio` dual-backend split (`uartPinConfigure_hw`/`_pio`,
    `uartSelectFunction_hw`/`_pio`) - i.e. some UARTs are real hardware UARTs
    and others are bit-banged via PIO state machines, doubling the usable
    serial port count on RP2350. This is a substantial, genuinely new
    subsystem port (not a naming-drift fix) - needs its own scoped design
    pass (read `serial_impl.h`/`serial_uart_impl.h`/`uartDevice_t` in
    betaflight fully, decide whether PIO-UART is in scope for first bring-up
    or can be deferred/stubbed to hardware-UART-only initially) before
    editing starts. **Not investigated further yet.**

### Sixth build iteration — first RP2350B linked image

- Initialized the `lib/modules/pico-sdk` submodule recursively in this checkout;
  the source tree previously had only the gitlink, so `hardware/*.h` includes
  could not resolve locally.
- Fixed RP2350-only build drift against pico-sdk 2.3.0: removed stale
  `rp2_common/pico_stdio_usb/reset_interface.c`,
  `common/pico_util/datetime.c`, and the incompatible pico-sdk
  `pico_clib_interface/newlib_interface.c` shim from `PICO_LIB_SRC`; added the
  missing `hardware_dcp/include` include directory; and changed
  `pico_flash_mem_defaults.ld` comments from `#` to linker-script comments so
  the current flat build can pass it directly to `ld`.
- Added PICO-safe compatibility in shared code: BASEPRI intrinsics in
  `build/atomic.h`, PICO `U_ID_0/1/2` aliases backed by `systemUniqueId`, PICO
  no-op ITM printf init, guarded generic UART DMA byte-counting, guarded
  generic SPI clock/divider and duplicate SPI DMA/pinconfig functions, and
  fixed `system_rp2350.c` reset calls to pass Wingflight reset reasons.
- Tightened RP2350 target bring-up flags: serial count is now USB VCP + two
  hardware UARTs, I2C defaults use `I2C_FULL_RECONFIGURABILITY`, unsupported
  STM32-timer features are disabled for now (`USE_PPM`, `USE_PWM`, camera
  control, serial passthrough), and USB-MSC is temporarily deferred as noted in
  Phase 5.
- Added PICO-safe build fallbacks for optional/no-backend paths:
  `adcinternal.c` returns `0` for core temperature when internal ADC support is
  off, ACC/GYRO detection consume unused probe parameters when no sensor driver
  is enabled, LED adjustment consumes its unused value without LED strip, and
  `pg/servos.c` initializes servo IO tags to `IO_TAG_NONE` instead of querying
  STM32 timer mappings while `flight/servos.c` skips the STM32 timer-output path.
- Verification 2026-08-25: `make TARGET=RP2350B` now succeeds and produces
  `obj/wingflight_4.6.0_RP2350B.hex`; `make TARGET=STM32F405` also succeeds,
  confirming the shared-code guards did not break an existing STM32 target.
- Remaining high-priority follow-ups: implement real PICO servo PWM output in
  `flight/servos.c`, restore TinyUSB MSC with a PICO-native storage adapter,
  add a `.uf2` post-build step, and validate RP2350B boot/USB/UART on hardware.

### Seventh iteration (2026-08-26) — RP2354A/RP2354B build break fix + cleanup

- Found that `RP2354A`/`RP2354B` did not compile at all: `adc_pico.c` branched
  on `#if defined(RP2350A) / #elif defined(RP2350B) / #else #error`, with no
  RP2354 case, even though `target.mk` defines `RP2354A`/`RP2354B` and
  `io_def_generated.h` already handles them correctly. Since RP2354A/B are
  pin-identical to RP2350A/B (same package, only the flash is on-die), fixed
  by adding `|| defined(RP2354A)` / `|| defined(RP2354B)` to the ADC
  channel-count/internal-temp-channel macros and the pin-to-channel mapping.
- Grepped for the same pattern elsewhere and found three more spots that
  compiled fine for RP2354 but would have silently used the wrong (30/40-pin
  A-package) pin list instead of the correct 48-pin B-package one:
  `bus_i2c_pico.c` (`numPins` for SDA/SCL validation), `bus_spi_pico.c` (extra
  SCK/MISO/MOSI pins on SPI0/SPI1), and `serial_uart_pico.c` (extra RX/TX pins
  on UART0/UART1). All changed from `#ifdef RP2350B` to
  `#if defined(RP2350B) || defined(RP2354B)` to match the existing convention
  already used in `io_def_generated.h`.
- Verification 2026-08-26: clean `make clean` + build of all four targets
  (`RP2350A`, `RP2350B`, `RP2354A`, `RP2354B`) succeeds with 0 errors.
- Cleanup: deleted the dead/unreferenced `src/main/drivers/uart_pico/`
  directory (superseded by the top-level `serial_uart_pico.c` in the UART
  refactor but left on disk, duplicating a filename and risking edits to the
  wrong file), removed the stray committed `build_rp2350b.log` artifact, and
  dropped the stale `MSC` entry from `FEATURES` in `target.mk` (USB-MSC is
  `#undef`'d in `target.h` and `MSC_SRC` is empty, so the feature flag was
  misleading dead weight).
- Still not addressed by this pass (see follow-ups above and Phase 5/6):
  RP2354 boot_stage2/QMI on-die-flash defaults are still unverified against
  real hardware, no target has been flash-tested, and there is still no CI
  coverage for any RP2350/RP2354 target.

### Eighth iteration (2026-08-26) — UF2 post-build step

- Added the `.uf2` post-build step flagged as "not started" earlier in this
  doc (Phase 5, line ~136). Since pico-sdk 2.x no longer vendors a standalone
  `elf2uf2` tool (it was folded into the separate `picotool` repo, not present
  in this tree, and pulling it in would mean a whole extra CMake/libusb C++
  project just to reimplement a small binary format), wrote a small
  self-contained `src/utils/bin2uf2.py` instead, following the same pattern
  as the existing `dfuse-pack.py` (.hex → .dfu) Python post-processing step.
  It reads the already-produced flat `.bin`, pads to 256-byte chunks, and
  emits standard 512-byte UF2 blocks per pico-sdk's `boot/uf2.h` layout, using
  family ID `0xe48bff59` (`RP2350_ARM_S_FAMILY_ID`) and base address
  `0x10000000` (`FLASH_ORIGIN` from `pico_rp2350_memory.ld`, also the lowest
  LMA of the linked ELF, which is what `objcopy -O binary` uses as byte 0).
- Wired into `Makefile`: new `TARGET_UF2`/`BIN2UF2` vars, a
  `$(TARGET_UF2): $(TARGET_BIN)` rule mirroring the DFU rule, a `uf2:`
  convenience target, and `.DEFAULT_GOAL` now resolves to `uf2` when
  `TARGET_MCU == RP2350` (STM32/other targets keep defaulting to `hex`,
  unaffected). `UF2_FAMILY_ID`/`UF2_BASE_ADDR` are defined in
  `make/mcu/RP2350.mk` so the Makefile rule itself stays MCU-agnostic.
- Verification 2026-08-26: clean builds of all four targets now produce a
  valid `.uf2` by default (819 blocks each, matching the ~205 KB `.bin`);
  manually parsed the UF2 block headers of the RP2350B output and confirmed
  correct magic numbers, sequential `target_addr` starting at `0x10000000`,
  and the RP2350 ARM-Secure family ID. `make TARGET=STM32F405` was re-verified
  to still default to `hex` only, with no `.uf2` produced.
- Not done: no on-hardware BOOTSEL flash test yet (no hardware available in
  this environment) — the UF2 has not been drag-and-drop verified against a
  real RP2350/RP2354 board, only structurally validated.

### Ninth iteration (2026-08-26) — CI coverage + IOConfigGPIO open-drain

- Added `RP2350A`/`RP2350B`/`RP2354A`/`RP2354B` to `.github/workflows/pr.yml`
  and `push.yml` (recursive submodule checkout added so `pico-sdk` is
  present, plus a dedicated `make TARGET=RP2350B uf2` step and updated
  artifact glob/upload paths). This is exactly the check that would have
  caught the RP2354 ADC/I2C/SPI/UART pin-macro build break fixed in the
  seventh iteration - verified locally by running the same invocations CI
  uses (`make RP2350A RP2350B RP2354A RP2354B FLASH_CONFIG_ERASE=yes`, etc.)
  before committing the workflow changes.
- Fixed `IOConfigGPIO()` in `io_pico.c`, which had a stale header comment
  claiming pull-up/pull-down were unimplemented (they were actually already
  wired via `gpio_set_pulls()`) but which genuinely never implemented
  open-drain: `IOCFG_OUT_OD`/`IOCFG_AF_OD` were bit-identical to their
  push-pull counterparts, so RP2 GPIO would always be configured push-pull
  regardless of the caller's request. This matters for real correctness, not
  just style: `drivers/bus_i2c_utils.c`'s `i2cUnstick()` (shared I2C bus
  recovery routine, used whenever an I2C peripheral needs to bit-bang its way
  out of a stuck bus) explicitly reconfigures SCL/SDA as `IOCFG_OUT_OD` and
  relies on `IORead()` seeing a peer's clock-stretching hold-low - which is
  impossible if the pin is actually driven push-pull.
  - Added a 4th open-drain bit to PICO's `IO_CONFIG()` encoding in
    `drivers/io.h` (RP2 has no native open-drain output mode, unlike STM32's
    `GPIO_OType_OD`), and implemented emulation in `io_pico.c` via direction
    toggling: "high"/released now switches the pin to input (floats up via
    pull-up), "low" drives output with the value pre-set to 0 before enabling
    the output driver (glitch-free). Tracked per-pin in a new static
    `pinOpenDrain[]` array indexed by raw GPIO number.
  - Also fixed a related bug in the same function: `IOConfigGPIO()` only
    forced GPIO_FUNC_SIO when the pin's current function was `GPIO_FUNC_NULL`
    (never-yet-initialized); if a pin was already routed to a peripheral
    function (e.g. `GPIO_FUNC_I2C`, exactly the case `i2cUnstick()` hits when
    reclaiming SCL/SDA from the hardware I2C block), it only printed a
    warning and left the peripheral function in place, silently making the
    subsequent `gpio_put()`/`gpio_set_dir()` calls no-ops on the actual pin.
    Now unconditionally switches to `GPIO_FUNC_SIO`, matching
    `IOConfigGPIO()`'s contract on every other platform (STM32's version
    unconditionally reclaims the pin from AF too).
- Verification 2026-08-26: clean rebuilds of all four RP2350/RP2354 targets
  and `STM32F405` (to confirm the shared `io.h` change doesn't affect
  non-PICO platforms, which use a different `IO_CONFIG()` overload) all
  succeed. Not hardware-tested (no board available) - the open-drain
  emulation logic was verified by inspection against pico-sdk's
  `gpio_set_dir`/`gpio_put`/`gpio_get` semantics and against `i2cUnstick()`'s
  actual usage pattern, not on a scope.

### Tenth iteration (2026-08-26) — real PICO servo PWM output

- Implemented the servo PWM output flagged as a high-priority follow-up since
  the sixth iteration ("implement real PICO servo PWM output in
  `flight/servos.c`"). The old `drivers/pwm_servo_pico.c` (excluded from the
  build, per the fifth iteration's note that it "targets a non-existent
  Wingflight servo-device API") called `servoDevInit()`/`servoWrite()`
  against a `servoDevConfig_t` type that doesn't exist anywhere in this
  codebase - confirmed by grep, that type/API pair is also dead/uncalled in
  `target/SITL/target.c`, so it was never a real integration point to begin
  with, just a speculative stub copied from a differently-structured
  upstream.
- The actual live servo API (`flight/servos.c`) writes output via a raw
  `timerChannel_t.ccr` register pointer (`*servoChannel[index].ccr = pos *
  servoResolution[index]`, `pos` in microseconds), populated by
  `pwmOutConfig()`/`timerAllocate()` - both STM32-timer-register concepts
  with no PICO equivalent (`timerHardware_t` has no PICO branch). Since
  `servoInit()` previously just set `servoCount = 0` and returned without
  populating `servoChannel[]`, `ccr` stayed NULL and every `servoSetOutput()`
  call was already a silent, safe no-op on PICO - servo mixer/PID logic ran
  every loop, but no hardware ever moved.
- Rewrote `pwm_servo_pico.c` (new `drivers/pwm_servo_pico.h` declares the
  interface) against RP2's real PWM-slice hardware, following the exact
  pattern already proven working for motor PWM output in
  `pwm_motor_pico.c`'s `PWM_TYPE_STANDARD` case (same 1 ms-centered pulse
  concept, same clkdiv/wrap-resolution-maximizing math) rather than STM32
  timer registers: `picoServoDevInit(ioTags)` configures one PWM slice/
  channel per servo pin (stopping at the first `IO_TAG_NONE`, matching the
  STM32 loop's convention) at that servo's own `servoParams()->rate`, and
  returns the count instead of touching a shared struct instance;
  `picoServoWrite(index, pos)` converts a microsecond pulse width to a
  compare level via a per-servo ticks-per-µs factor (the PICO equivalent of
  `servoResolution[]`); `picoServoShutdown()` zeroes every configured channel.
- Wired into `flight/servos.c` with three `#if defined(PICO)` branches
  (`servoInit()`, `servoSetOutput()`, `servoShutdown()`) alongside the
  existing STM32 code, and guarded the now-STM32-only `servoResolution[]`/
  `servoChannel[]` file-scope arrays behind `#if !defined(PICO)` (they'd
  otherwise trip `-Werror=unused-variable` on PICO, since PICO no longer
  references them at all). Added `drivers/pwm_servo_pico.c` back to
  `make/mcu/RP2350.mk`'s source list (it had been dropped when the old,
  API-mismatched version was excluded).
- Verification 2026-08-26: clean builds of all four RP2350/RP2354 targets
  succeed and link in the new driver; `STM32F405` rebuilt to an
  identical `.elf` size as before this change, confirming zero impact on the
  STM32 path. Not hardware-tested (no board, and no servo pins are wired in
  `RP2350_UNIFIED/target.h` by default - actual pin assignment is per-board
  CLI config like every other unified target) - the PWM timing math itself
  mirrors `pwm_motor_pico.c`'s already-used-in-this-port formula, so it's
  verified by inspection/analogy, not on a scope.

### Eleventh iteration (2026-08-26) — DSHOT actually enabled (was silently dead code)

- Major finding: **`USE_DSHOT` was never defined for `RP2350_UNIFIED`.**
  `target.h` already anticipated DSHOT everywhere (`PIO_DSHOT_INDEX`,
  `USE_DSHOT_TELEMETRY`, `#undef USE_DSHOT_BITBANG`) and `dshot_pico.c`/
  `dshot_bidir_pico.c` are fully-implemented PIO drivers (the latter has a
  substantial custom GCR-decode/auto-calibration telemetry engine, not from
  upstream Betaflight), both listed in `RP2350.mk`'s source list since early
  iterations - but without `USE_DSHOT` itself, `#ifdef USE_DSHOT` compiled
  both files down to nothing every time, on every build, silently. Confirmed
  by checking the generated `.d` dependency file for `dshot_pico.o`: it
  listed nothing but `platform.h`'s own include chain - `dshot_pico.h` was
  never even reached. Until this iteration, **no RP2350/RP2354 build has
  ever had motor output beyond plain PWM/OneShot** (`pwm_motor_pico.c`),
  regardless of anything configured in the CLI.
- Turning `USE_DSHOT` on (`target.h`) surfaced that `dshot_pico.c`/
  `dshot_pico.h`/`dshot_bidir_pico.c` were byte-for-byte straight copies of
  upstream Betaflight's `src/platform/PICO/dshot_pico.c` (confirmed against
  the `betaflight/` reference checkout in this repo) - written against a
  *newer* Betaflight motor API (`drivers/motor_types.h`'s
  `motorProtocolTypes_e`, and a `motorVTable_t` with
  `telemetryWait`/`decodeTelemetry`/`updateInit`/`isMotorIdle`/
  `requestTelemetry`/`convertExternalToMotor`/`convertMotorToExternal`) that
  Wingflight, a Rotorflight-lineage fork, never adopted. Wingflight's real,
  working contract (`drivers/motor.h`, proven by `motor.c`, `dshot_bitbang.c`,
  `pwm_output_dshot_shared.c`) is older/simpler: `motorPwmProtocolTypes_e`
  (`PWM_TYPE_DSHOT150/300/600`, not upstream's `MOTOR_PROTOCOL_*`), and a
  `motorVTable_t` with just `postInit/enable/disable/shutdown/updateStart/
  updateComplete/write(index,mode,value)/writeInt/isMotorEnabled` - no
  telemetry-specific vtable slots at all (telemetry decode instead happens
  inside `updateStart()`, and cross-backend command/telemetry-request state
  is shared via `getMotorDmaOutput()`, not a vtable callback). This is the
  same class of upstream-API-mismatch already found and fixed for
  `pwm_servo_pico.c` (tenth iteration) and dead `servoDevInit`/`pwmWriteServo`
  in `target/SITL/target.c` - evidently a recurring hazard whenever a file is
  ported by literal copy from upstream rather than adapted to Wingflight's
  actual (older) driver contracts, and made worse here because `USE_DSHOT`
  being off meant the mismatch could never surface as a compile error.
- Rewrote `dshot_pico.h`/`dshot_pico.c`/`dshot_bidir_pico.c` against the real
  API, keeping the PIO logic/timing and the bidir GCR-telemetry decode
  algorithm entirely intact (only their *plumbing* into the rest of the
  firmware changed):
  - `dshotPwmDevInit()` now returns `motorDevice_t *` (`NULL` on failure)
    taking `(motorDevConfig, motorCount)` directly, matching
    `motorPwmDevInit()`/`dshotBitbangDevInit()`'s real signature, instead of
    the old `bool dshotPwmDevInit(motorDevice_t *device, ...)`.
  - `dshotVTable` now has exactly Wingflight's 9 real fields; `dshotWrite()`
    takes `(index, mode, value)` and calls the shared `dshotConvertToInternal()`
    (used identically by bitbang/HAL); telemetry decode + the old
    `dshotUpdateInit()`'s per-cycle packet-buffer reset both moved into a new
    `dshotUpdateStart()`, which is the real vtable hook for this (there is no
    `updateInit` slot in Wingflight's `motorVTable_t`).
  - Added `getMotorDmaOutput()` (backed by a small PICO-local
    `dshotDmaMotors[]` using the *real* `motorDmaOutput_t` from
    `drivers/dshot_dpwm.h` - only `->protocolControl` is meaningful, the rest
    is unused STM32-DMA bookkeeping) since `dshot_command.c`'s
    `allMotorsAreIdle()`/`dshotCommandWrite()` call it *unconditionally*
    (regardless of backend) to manage command/telemetry-request state - this
    is a real, load-bearing cross-backend contract, not upstream-only cruft.
  - `dshot_bidir_pico.c`'s telemetry decode now writes into Wingflight's real
    `dshotTelemetryState.motorState[i].telemetryValue`/`.telemetryActive`
    (not upstream's `.rawValue`/`.telemetryTypes`/`dshotRawValueState_t`,
    none of which exist here), dropped the unsupported extended-telemetry-type
    branch, and added `isDshotMotorTelemetryActive()`/`isDshotTelemetryActive()`
    (normally defined in `pwm_output_dshot_shared.c`, which is excluded for
    PICO - see below - but called unconditionally from `cli.c`/`msp.c`
    whenever `USE_DSHOT_TELEMETRY` is defined).
  - Removed the DSHOT600-only restriction the user asked to lift: since
    `dshotGetPeriodTiming()`/`dshot_program_init()`/`dshot_program_bidir_init()`
    already scale the PIO clock divider from the selected protocol's real bit
    period (the PIO programs' cycle counts are protocol-agnostic - "DSHOT600"
    in the program names is just historical), DSHOT150/300 needed no PIO
    changes at all, only removing the artificial gate in `dshotPwmDevInit()`.
    The user confirmed 4 motors max is fine, so that existing cap is
    untouched.
  - `make/mcu/RP2350.mk`'s `MCU_EXCLUDES` gained `dshot_dpwm.c`,
    `pwm_output_dshot.c`, `pwm_output_dshot_shared.c`, `pwm_output_dshot_hal.c`,
    `pwm_output_dshot_hal_hal.c` - all STM32 DMA/timer-register DSHOT backends
    that would otherwise either duplicate-define `dshotPwmDevInit()`/
    `useDshotTelemetry` against `dshot_pico.c`, or fail outright (they need
    `timerAllocate()`, excluded since the sixth iteration).
- Turning on `USE_DSHOT`/`USE_DSHOT_TELEMETRY` also surfaced three unrelated,
  previously-latent gaps in generic (non-PICO-specific) code - each is a real
  bug on *any* platform that has DSHOT+telemetry but not full STM32 timer/DMA
  hardware, just never previously exercised because no such Wingflight target
  existed before RP2350:
  - `drivers/dshot_dpwm.h`'s `motorDmaOutput_t` uses `TIM_OCInitTypeDef`/
    `TIM_ICInitTypeDef` *by value*, but `common/platform.h`'s PICO
    compatibility shim only stubbed `TIM_OCInitTypeDef` as `typedef void`
    (incomplete - can't be used by value) and didn't stub
    `TIM_ICInitTypeDef` at all. Both are now real (if inert) empty-struct
    stubs, matching the existing `DMA_InitTypeDef` precedent in the same
    file. This header is reachable from genuinely generic code
    (`drivers/dshot.c`, `cli.c`) any time `USE_DSHOT` is defined, regardless
    of backend.
  - `config/config.c`'s `validateAndFixConfig()` unconditionally calls
    `timerGetConfiguredByTag()` (only defined in `drivers/timer_common.c`,
    excluded for PICO since the sixth iteration) under
    `#if defined(USE_DSHOT_TELEMETRY)` to check for N-channel timer usage
    (an STM32 burst-DMA-safety concept). Guarded just that loop with
    `#if !defined(PICO)`, leaving `nChannelTimerUsed = false` otherwise.
  - `sensors/esc_sensor.c`'s AM32/BLHeli "ESC forward programming" feature
    (reading/writing ESC parameters over bidirectional DSHOT,
    `USE_AM32_FORWARD_PROGRAMMING`/`USE_BLHELI_FORWARD_PROGRAMMING`, both on
    by default) calls `fwifCmdDevice*()`/`esc4wayInit()` unconditionally,
    but those are only ever defined in `io/serial_4way.c`, itself gated by
    `USE_SERIAL_4WAY_BLHELI_INTERFACE` - which `RP2350_UNIFIED/target.h`
    already (and correctly) disables. The dependency between the two
    feature flags was never enforced, so it only broke once something
    (`USE_DSHOT`) finally made the reference reachable. Disabled
    `USE_AM32_FORWARD_PROGRAMMING`/`USE_BLHELI_FORWARD_PROGRAMMING` for
    RP2350 to match - this is a real, if minor, feature loss (ESC parameter
    read/write via the Configurator won't work) versus upstream, deferred
    until `io/serial_4way.c`'s bootloader protocol is ported to a
    PICO-compatible I/O path.
  - Similarly, `sensors/esc_sensor.c`'s raw-telemetry recorder calls
    `blackboxLogCustomData()` unconditionally, but `blackbox.c` (and hence
    its definition) is entirely `#ifdef USE_BLACKBOX`-gated, and
    `USE_BLACKBOX` is undef'd for RP2350 (deferred since the sixth
    iteration). Added a no-op fallback definition in `blackbox.c`'s
    `#else` branch.
- Verification 2026-08-26: clean builds of all four RP2350/RP2354 targets
  succeed and **link** (the real proof this all actually wired up, unlike
  the silent no-op state this branch has been in since inception).
  Re-verified `STM32F405`/`STM32F7X2`/`STM32H743`/`STM32G47X` (one target per
  MCU family/toolchain: non-HAL F4, and HAL F7/H7/G4) all rebuild to
  identical `.elf` sizes as before this iteration, confirming the
  `common/platform.h`/`config/config.c`/`blackbox.c` changes (all shared,
  non-PICO-specific files) have zero effect on any existing platform.
- Not done / not hardware-tested: no ESC available in this environment to
  verify DSHOT150/300/600 output or bidirectional telemetry decode timing
  for real - this is motor-control-critical code, so real-hardware
  validation (oscilloscope/logic analyzer on the signal line at minimum,
  then a real ESC+motor) before flight is strongly recommended before
  relying on this. AM32/BLHeli ESC parameter forward-programming is now a
  known, explicit gap (see above) rather than a silent link failure.

### Twelfth iteration (2026-08-26) — DSHOT/dead-code cleanup pass

- Surveyed USB-MSC and multicore (both flagged as deferred follow-ups) to
  scope the next task; found both are blocked on the same missing
  prerequisite - RP2350 has no blackbox/log storage backend at all today
  (`USE_BLACKBOX` still undef'd), so there's nothing for MSC to expose and
  no real consumer for multicore's task-offload design yet. Also found a
  real (separate, unrelated) bug worth flagging for later: `pico_rp2350_memory.ld`'s
  `PRIMARY_FLASH_LENGTH` default (4M) doesn't match either RP2350A/B's 8MB
  external QSPI or RP2354A/B's 2MB on-die flash - not fixed this iteration
  (user chose to defer both the flash-blackbox feature and this bug fix in
  favour of a cleanup pass instead), but noted here so it isn't lost.
- `dshot_pico.c`'s `dshotShutdown()` had a stale `// TODO: implement?` left
  over from the eleventh iteration's straight-upstream-copy origins; clarified
  it's intentionally a no-op, matching `dshot_dpwm.c`'s `dshotPwmShutdown()`
  for the STM32 backend (both rely on `motor.c`'s `motorShutdown()` clearing
  `motorDevice->enabled` before any further write can reach the backend).
- Removed `src/main/drivers/debug_pin.c`: a dead, unreferenced duplicate of
  the real, compiled `src/main/build/debug_pin.c` (different license-header
  style matching the other straight-ported `*_pico.c` files, ~40% smaller,
  never listed in any Makefile) - same class of leftover-porting-artifact
  risk as the `uart_pico/` folder removed in the seventh iteration. It never
  caused a problem because `USE_DEBUG_PIN` is off by default, so neither
  copy's body actually compiles either way - but having two files defining
  the same `dbgPinInit/dbgPinHi/dbgPinLo` API risked a future edit landing
  in the wrong (dead) one.
- Fixed `exti_pico.c`'s `EXTIConfig()`, which had a `// TODO consider
  pullup/pulldown etc. Needs fixing first in platform.h` comment dating from
  before the ninth iteration's `IOConfigGPIO()` open-drain/pull work landed
  the actual prerequisite (the PICO `IO_CONFIG()` pull-bit encoding in
  `drivers/io.h`). Now decodes and applies the same pull-up/pull-down bits
  `IOConfigGPIO()` does via `gpio_set_pulls()`, instead of silently ignoring
  the `ioConfig_t` passed in - this matters for e.g. an interrupt-driven
  gyro `DRDY` pin configured with `IOCFG_IPU`/`IOCFG_IPD`, which previously
  had its requested pull silently dropped when wired through `EXTIConfig()`
  rather than `IOConfigGPIO()` directly.
- Verification 2026-08-26: clean builds of all four RP2350/RP2354 targets
  and `STM32F405` succeed after all of the above.

### Thirteenth iteration (2026-08-26) — flash-size linker bug fix + blackbox storage decision

- Fixed the `PRIMARY_FLASH_LENGTH` bug flagged in the twelfth iteration.
  `src/link/pico_flash_mem_defaults.ld` unconditionally set
  `PRIMARY_FLASH_LENGTH = 4M;` regardless of target, so every RP2350/RP2354
  variant computed its `FLASH`/`FLASH_CONFIG`/`FLASH_FONT` region sizes from
  the same wrong assumption - not just cosmetically wrong: on RP2354A/B
  (2MB on-die flash), `FLASH_CONFIG` would land at `4M - 64K`, an address
  with no physical flash behind it at all, so `config_streamer.c`'s
  `flash_range_erase()`/`flash_range_program()` calls would have targeted
  memory that doesn't exist on that chip.
  - Made both assignments in `pico_flash_mem_defaults.ld` conditional via
    `DEFINED(...) ? ... : ...` instead of a plain `=`, so a value already
    established via `--defsym` on the link command line isn't clobbered.
  - `make/mcu/RP2350.mk` now passes
    `-Wl,--defsym=PRIMARY_FLASH_LENGTH=<bytes>`, computed from the
    already-known per-variant `MCU_FLASH_SIZE` (8192 for RP2350A/B, 2048 for
    RP2354A/B, both in KB) via `$(shell echo $$(( $(MCU_FLASH_SIZE) * 1024 )))`.
    Note: the more obvious `$(shell expr $(MCU_FLASH_SIZE) \* 1024)` does
    **not** work in this environment - Make hands the shell an unescaped
    `*`, which glob-expands against the working directory's files
    (`expr: syntax error: unexpected argument 'AGENTS.md'`) instead of
    multiplying. The `$(( ... ))` arithmetic-expansion form sidesteps this
    since `*` inside it is never subject to pathname expansion.
  - Verification: rebuilt all four targets and confirmed via
    `--print-memory-usage`'s "Memory region" table that `FLASH` is now
    correctly `8128 KB` on RP2350A/B (8192K - 64K config) and `1984 KB` on
    RP2354A/B (2048K - 64K config), instead of the previous ~4032K on all
    four regardless of real chip size. `STM32F405` rebuilds to an identical
    `.elf` size, confirming no effect on non-PICO targets (this bug/fix is
    entirely inside `make/mcu/RP2350.mk`/PICO-specific linker scripts).
- **Blackbox storage medium decided: a dedicated external SPI/QSPI flash
  chip and an SD card - not the on-die/QSPI firmware flash.** Recorded as a
  Phase 0 decision and cross-referenced from Phase 5's USB-MSC entry (see
  above); this explicitly rules out the "carve a `FLASH_BLACKBOX` region out
  of firmware flash" option the twelfth iteration's survey had been
  assessing, which avoids both of that option's real problems (multicore
  XIP-write safety, and RP2354A/B having no real flash headroom) rather than
  solving them. Implementation should reuse Wingflight's existing
  `flashVTable_t`/`sdcard_spi.c` abstractions against a real PICO SPI bus,
  not a new PICO-specific storage type - not started this iteration.

### Fourteenth iteration (2026-08-26) — bus_spi_pico.c 16-bit transfer optimisation

- Resolved the `// TODO optimise with 16-bit transfers as per stm bus_spi_ll
  code` in `spiInternalReadWriteBufPolled()`. The PL022 SPI block (used by
  both RP2350 and STM32's own SPI peripherals) supports 4..16 data bits per
  transfer; the STM32 LL driver already exploits this by clocking 16 bits at
  a time for the bulk of a polled transfer, only falling back to 8-bit mode
  for a trailing odd byte - halving the number of TX/RX-FIFO polling
  round-trips for even-length transfers. Ported the same idea to
  `bus_spi_pico.c` using pico-sdk's `spi_write16_read16_blocking()`/
  `spi_write16_blocking()`/`spi_read16_blocking()` (word-count-based
  siblings of the existing byte-based calls).
- The one PICO-specific wrinkle: toggling data width requires
  `spi_set_format(spi, bits, cpol, cpha, order)`, which also takes CPOL/CPHA
  - but this function doesn't otherwise know how the bus was configured.
  Rather than threading that state through, it reads the current CPOL/CPHA
  directly off the PL022's CR0 register (`SPI_SSPCR0_SPO_BITS`/
  `_SPH_BITS`) before switching to 16-bit and restores them unchanged
  afterwards; `spi_set_format()` itself is cheap (a masked CR0 write while
  briefly clearing/restoring the SPI-enable bit, no FIFO flush), so this
  format toggle doesn't undermine the point of the optimisation.
- Verification 2026-08-26: all four RP2350/RP2354 targets build cleanly.
  This file is PICO-only (not shared with STM32), so no cross-platform
  regression risk. Not exercised on real hardware/a logic analyzer - this
  changes low-level SPI bus timing for every PICO SPI peripheral (gyro,
  external flash, SD card, once wired up), so it's worth confirming with a
  scope on a real bus before relying on it, same caveat as the DSHOT work.

### Fifteenth iteration (2026-08-26) — sensor drivers + storage wired up generically

- Corrected a wrong assumption from earlier iterations: RP2350/RP2354 don't
  need any board-specific IMU/pin knowledge to wire up sensors or storage.
  Wingflight (like upstream Betaflight/Rotorflight) is a fully generic,
  config-driven unified-target firmware - `src/main/target/STM32_UNIFIED/
  target.mk` compiles in *every* accgyro/barometer/compass driver via a
  wildcard glob and lets runtime auto-detection (driven entirely by a CLI
  `.config` file's `resource GYRO_CS`/`GYRO_EXTI`, `set gyro_1_bustype`/
  `gyro_1_spibus`, `baro_hardware`, `blackbox_device`/`flash_spi_bus`, etc. -
  see e.g. wingflight-targets' `FRSK-VANTAC_RF007.config`) pick whichever
  chip is actually present on a given board. RP2350_UNIFIED just needed the
  same generic wiring; no specific board's pin/chip details were needed at
  all. `RP2350_UNIFIED/target.mk`'s `TARGET_SRC = ` had been left empty with
  a comment deferring this "to a later porting phase" since the fifth
  iteration - that phase is this one.
- `RP2350_UNIFIED/target.mk`: `TARGET_SRC` now mirrors `STM32_UNIFIED/
  target.mk`'s wildcard glob over `drivers/accgyro/*.c` /
  `drivers/barometer/*.c` / `drivers/compass/*.c` (plus the BMI270 vendor
  driver they both need). `FEATURES` gained `SDCARD_SPI ONBOARDFLASH`
  (SPI-only - PICO has no SDIO peripheral, so no `SDCARD_SDIO`, matching the
  decision that blackbox storage is a dedicated external SPI flash chip
  and/or SD card - see the thirteenth iteration).
- `RP2350_UNIFIED/target.h`: added the same `USE_ACC_*`/`USE_GYRO_*`/
  `USE_MAG`/`USE_MAG_*`/`USE_BARO`/`USE_BARO_*`/`USE_SDCARD(_SPI)`/
  `USE_FLASHFS`/`USE_FLASH_*` block as `STM32_UNIFIED/target.h`, replacing
  the previous bare `#define USE_ACC` with no chip enabled at all (so no
  gyro driver could ever have matched, even with I2C/SPI bus config
  correct) and the `#undef USE_BARO`/`#undef USE_COMPASS` guards (the latter
  turned out to be dead/unused anywhere in the codebase - real magnetometer
  support is gated by `USE_MAG`). Also re-enabled `USE_BLACKBOX` (previously
  undef'd since it had nothing to write to) now that a real storage backend
  is in scope.
- Iteratively fixed the real compile fallout from turning all this on (same
  build-driven approach as the DSHOT work, though this time only three
  issues surfaced instead of ~ten):
  - `drivers/bus_spi.h` had `SPI_IO_CS_CFG` defined only for STM32 families;
    generic code that configures a device's own chip-select pin as a plain
    GPIO output (`accgyro_mpu.c`, `flash.c`, `sdcard_spi.c`) needs it on any
    platform. Added a PICO branch aliasing it to the already-existing
    `IOCFG_OUT_PP` (PICO's SPI driver configures SCK/MISO/MOSI directly via
    `gpio_set_function()`, not through an AF `IOConfigGPIO()` value, so no
    `SPI_IO_AF_*` equivalents were needed).
  - `drivers/barometer/barometer_qmp6988.c` had a genuine, pre-existing,
    platform-independent bug: several calibration-coefficient expressions
    mixed bare (`double`) floating-point literals with `(float)`-cast
    operands, which is exactly what `-Wdouble-promotion` (enabled globally,
    `Makefile:281`) flags. It had simply never been caught before because no
    platform previously both defined `USE_BARO_QMP6988` *and* had this
    warning trip an actual compile of the file with the RP2350 toolchain
    exercising this path for the first time (STM32_UNIFIED defines the same
    macro, so this may be a live, if minor, latent bug there too - worth a
    quick check independent of PICO). Fixed by adding `f` suffixes to the
    literals so the arithmetic stays in `float` throughout.
  - `drivers/compass/compass_ak8963.c` called bare CMSIS-core
    `__disable_irq()`/`__enable_irq()` around a brief SPI-register-buffer
    read (an MPU6500/9250-as-I2C-master AK8963 read path). PICO deliberately
    doesn't include the CMSIS device header (`common/platform.h`'s PICO
    section explains why: address-macro collisions with pico-sdk's own
    headers), so these aren't available - added a `#if defined(PICO)`
    branch using pico-sdk's `save_and_disable_interrupts()`/
    `restore_interrupts()` (`hardware/sync.h`) as the equivalent
    global-IRQ critical section, leaving the STM32 `__disable_irq()`/
    `__enable_irq()` path untouched.
- Verification 2026-08-26: clean builds of all four RP2350/RP2354 targets
  (RAM usage jumped from ~51% to ~67% with every sensor driver + flash/SD
  storage compiled in - worth watching if more features get added later,
  but well within budget for now). Re-verified `STM32F405`/`STM32F7X2`/
  `STM32H743`/`STM32G47X` all rebuild to identical `.elf` sizes, confirming
  the three shared-file fixes above have zero effect on any existing
  platform.
- Not done / not hardware-tested: no IMU/baro/mag/flash-chip/SD-card
  available in this environment to verify auto-detection, bus timing, or
  actual sensor data on real hardware - this is exactly the kind of thing
  that needs a real board and a `.config` file (`resource`/`set` commands)
  to prove out. `MSC_SRC` is still empty in `RP2350.mk` (USB-MSC itself
  remains a separate, not-yet-tackled follow-up per the twelfth/thirteenth
  iterations - the storage *backend* it would expose now exists, but the
  TinyUSB MSC adapter still needs re-enabling and wiring to it).
