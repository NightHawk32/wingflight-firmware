# RP2350 Porting Plan & Progress Tracker

Status date: 2026-08-24
Owner: (assign when work starts)

This document tracks the plan to add Raspberry Pi RP2350 (RP2350A/B, Cortex-M33)
support to Wingflight, porting from the vendored upstream Betaflight snapshot in
[betaflight/](../betaflight/), specifically `betaflight/src/platform/PICO/`.

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
  library sources directly under `lib/main/<FAMILY>` instead. A decision is
  needed on which convention to follow (see Phase 0).

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

- [ ] Target variant: RP2350A or RP2350B first (pin count differs)?
- [ ] Specific board / reference design to bring up first?
- [ ] Vendor pico-sdk as a committed source snapshot into `lib/main/PICO`
      (matches Wingflight convention) vs. add it as a git submodule (matches
      Betaflight, easier upstream sync but a new pattern for this repo)?
- [ ] Drop OSD framebuffer, USB-MSC, and multicore support from the first pass?
      (None are obviously needed for a fixed-wing stabilizer; cutting them
      shrinks scope significantly.)

## Phase 1 — Toolchain feasibility spike

- [ ] Confirm `tools/gcc-arm-none-eabi-9-2020-q2-update` builds
      `-mcpu=cortex-m33 -march=armv8-m.main+fp+dsp` code.
- [ ] Link a minimal blinky against a pared-down pico-sdk subset outside the
      main build to prove toolchain viability before investing in driver ports.

## Phase 2 — Build system plumbing

- [ ] Vendor pico-sdk (+ TinyUSB) subset per Phase 0 decision.
- [ ] Create `make/mcu/RP2350.mk`, translating `RP2350.mk`'s variables into
      Wingflight's `ARCH_FLAGS`/`DEVICE_FLAGS`/`MCU_COMMON_SRC`/`VCP_SRC`
      conventions.
- [ ] Port linker scripts into `src/link/`.
- [ ] Add `.uf2` post-build step (Wingflight currently only emits hex/bin/elf).
- [ ] Register `RP2350` in [make/targets.mk](../make/targets.mk) MCU group logic.

## Phase 3 — Minimal bring-up target

- [ ] Clock init, GPIO, UART console, LED only.
- [ ] Validate boot / flash-via-uf2 before wiring in the full driver set.

## Phase 4 — Core drivers

- [ ] DMA
- [ ] Timer / PIO plumbing
- [ ] Motor / PWM (DSHOT via PIO)
- [ ] ADC
- [ ] I2C
- [ ] SPI
- [ ] Persistent config-in-flash

Each driver must be checked against Wingflight's existing driver headers
rather than assumed to be a drop-in copy from `betaflight/src/platform/PICO/`.

## Phase 5 — USB VCP (TinyUSB)

- [ ] Implement CLI/MSP over TinyUSB-based serial. This is net-new — Wingflight's
      existing `vcp`/`vcpf4`/`vcp_hal` are STM32 USB-OTG specific and don't apply.

## Phase 6 — Validation

- [ ] RX/telemetry serial protocols validation (mostly MCU-agnostic already).
- [ ] Mixer/servo validation on real fixed-wing hardware.
- [ ] Update [Changes.md](../Changes.md) / [README.md](../README.md) once RP2350
      support is functional.

## Progress Log

- 2026-08-24: Initial research completed and plan written. No implementation
  started yet. Phase 0 decisions still pending user input.
