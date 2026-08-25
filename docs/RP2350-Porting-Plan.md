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
      flight controller can expose config/blackbox storage as a mass-storage
      device over USB.
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

