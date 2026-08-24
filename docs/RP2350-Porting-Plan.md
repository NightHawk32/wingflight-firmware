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

- [ ] Confirm `tools/gcc-arm-none-eabi-9-2020-q2-update` builds
      `-mcpu=cortex-m33 -march=armv8-m.main+fp+dsp` code.
- [ ] Link a minimal blinky against a pared-down pico-sdk subset outside the
      main build to prove toolchain viability before investing in driver ports.

## Phase 2 — Build system plumbing

- [ ] Add `.gitmodules` entry for pico-sdk (pin to the commit used by
      `betaflight/.gitmodules` initially) under `lib/modules/pico-sdk`,
      matching Betaflight's path convention; tinyusb comes along as
      `lib/modules/pico-sdk/lib/tinyusb` (submodule-of-submodule, per upstream).
- [ ] Create `make/mcu/RP2350.mk`, translating `RP2350.mk`'s variables into
      Wingflight's `ARCH_FLAGS`/`DEVICE_FLAGS`/`MCU_COMMON_SRC`/`VCP_SRC`
      conventions. Shared by all four variants (variant-specific bits pushed
      into per-target `target.mk`, e.g. GPIO count for A vs B, flash origin
      for RP2350x vs RP2354x).
- [ ] Port linker scripts into `src/link/` for RP2350A/RP2350B (external QSPI
      flash, ported from betaflight); add new RP2354A/RP2354B memory maps
      (on-die flash, no external QSPI flash region).
- [ ] Add `.uf2` post-build step (Wingflight currently only emits hex/bin/elf).
- [ ] Register `RP2350A`, `RP2350B`, `RP2354A`, `RP2354B` in
      [make/targets.mk](../make/targets.mk) MCU group logic (all four mapped
      to `TARGET_MCU_FAMILY := RP2350` for shared build rules, per-target
      flash/memory/GPIO differences handled in each target.mk/target.h).

## Phase 3 — Minimal bring-up target

- [ ] Clock init, GPIO, UART console, LED only.
- [ ] Validate boot / flash-via-uf2 before wiring in the full driver set.
- [ ] Bring-up order: RP2350B first (straight port path, matches betaflight's
      primary variant), then RP2350A (same port path, smaller GPIO count),
      then derive RP2354B and RP2354A (on-die flash variants) once their
      RP2350 counterparts boot cleanly.

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
- [ ] Port multicore support (`PICO/multicore.c`) — evaluate whether to also
      enable `ENABLE_MULTICORE_INIT` (running FC init phases on core 1, per
      `target_RP2350.h` comments) or just core-1 task offload.

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
