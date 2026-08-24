# AT32F435 Support — Progress Tracker

Tracks the work needed to add Artery AT32F435 MCU support to Wingflight.
Reference: newer Betaflight platform-refactored source under `betaflight/` (used for
driver/register logic reference only — its build architecture is not compatible
with Wingflight's BF4.3-era layout and cannot be copy-pasted directly).

**Build model: unified target.** Wingflight already builds generic unified MCU targets
(see `src/main/target/STM32_UNIFIED/`, e.g. `TARGET=STM32F405`) — one binary per MCU with
no hardcoded board pinout, configured at runtime via CLI `resource`/`timer`/`dma`/`set`
commands loaded from a `.config` file (like [GSKY-GOOSKY_F4MINI.config](GSKY-GOOSKY_F4MINI.config)).
AT32F435 support should follow the same model: a new `AT32_UNIFIED` target family, not a
dedicated per-board target folder. No new board-specific target.c/target.h is needed;
end users flash the generic AT32F435 build and load their board's `.config`.

## Hardware notes

- [x] Reference config obtained: [GSKY-GOOSKY_F4MINI.config](GSKY-GOOSKY_F4MINI.config) —
      CLI dump from a real board (`Rotorflight / STM32F405 (S405) 4.5.1`,
      `board_name GOOSKY_F4MINI`, `board_design RTFL`, `manufacturer_id GSKY`).
      This is the STM32F405 version of the board, used here as the resource/pin
      reference for a prospective AT32F435 variant.
- [x] Not applicable under the unified-target model: `GOOSKY_F4MINI` doesn't need a
      dedicated target folder for either STM32F405 or AT32F435 — the `.config` file is
      loaded at runtime on top of the generic `STM32_UNIFIED`/`AT32_UNIFIED` build.
- [ ] Confirm whether an actual AT32F435 hardware variant of this board exists/is planned —
      need schematic/datasheet confirmation of matching pinout (crystal, USB, ADC dividers,
      boot-strap pins). Pin-package compatibility with STM32F405 is generally true for
      AT32F435, but register/peripheral IP is different (CRM vs RCC, different USB OTG core,
      different flash/ADC/timer implementation).
- [ ] Key resources to replicate/verify on an AT32F435 variant (from the reference config):
      - MCU: STM32F405, `system_hse_mhz = 8`
      - Gyro: SPI1 (`SCK A05 / MISO A06 / MOSI A07`, CS `C04`, EXTI `C05`), `CW0`,
        board align roll 180 / yaw 90
      - Flash: SPI3 (`SCK C10 / MISO C11 / MOSI C12`), CS `B03`, blackbox `SPIFLASH`
      - Motors: `MOTOR1 C08` (TIM8 CH3, DMA2 Stream4 Ch7), `MOTOR2 A08` (TIM1 CH1, DMA2 Stream1 Ch6)
      - Servos: `SERVO1 B04` / `SERVO2 B05` (TIM3 CH1/CH2), `SERVO3 B00` (TIM3 CH3)
      - UARTs: `UART1 TX B06/RX B07`, `UART2 TX A02/RX A03`, `UART5 RX D02`
      - I2C1: `SCL B08 / SDA B09`; LEDs `C14`/`C15`
      - DSHOT300 bidir, ESC sensor BLHeli32, `pid_process_denom = 4`
      - These pins/peripherals map to specific AT32F435 timer/DMA/SPI/USART instances that
        need to be confirmed against the AT32F435 datasheet (Artery's alternate-function/DMA
        tables differ from STM32F405's).

## 1. Vendor SDK

- [x] Import `lib/main/AT32F43x` (CMSIS + AT-BSP driver lib + USB stack) from
      `betaflight/lib/main/AT32F43x` into Wingflight's `lib/main/`. Done via
      `robocopy` — 560 files copied, verified against source count.
- [x] Verify license compatibility of vendored Artery SDK files. Confirmed: Artery's
      own permissive BSP license ("AS IS", authorizes use/copy/distribution for
      development with Artery MCUs, no redistribution restriction), same terms
      Betaflight already relies on.

## 2. Build system

- [x] Create `make/mcu/AT32F4.mk` modeled on [make/mcu/STM32F4.mk](make/mcu/STM32F4.mk),
      using the AT-BSP driver source list instead of StdPeriph/HAL. Note: `MCU_COMMON_SRC`
      lists the *planned* AT32 driver filenames — most don't exist yet (section 3), so this
      won't link/compile until those are ported.
- [x] Add `TARGET_MCU := AT32F4` branch in [make/targets.mk](make/targets.mk), with a new
      `F435_TARGETS` group added alongside `F4_TARGETS`/`F7_TARGETS`/`G4_TARGETS`/`H7_TARGETS`.
- [ ] Add AT32 startup/linker scripts (`startup_at32f435_437.s`, `at32_flash_f43x*.ld`)
      into `src/main/startup` and linker dir layout. `AT32F4.mk` already references
      `LD_SCRIPT`/`STARTUP_SRC` paths for these — they need to be created.
- [ ] Update [make/source.mk](make/source.mk) if any AT32-specific source filtering is needed.
- [x] Create `src/main/target/AT32_UNIFIED/` following the `STM32_UNIFIED/` pattern:
      - [x] `target.c` — empty stub (same as STM32_UNIFIED).
      - [x] `target.h` — full common unified feature block (mirrors STM32_UNIFIED's) plus an
        `#if defined(AT32F435)` section (UART1-8, SPI1-4, I2C1-3, `TARGET_IO_PORTA`-`H`,
        `TARGET_BOARD_IDENTIFIER "A435"`, `USBD_PRODUCT_STRING`).
      - [x] `AT32F435.mk` — empty alt-target marker file (mirrors `STM32F405.mk`).
      - [x] `target.mk` — adds `AT32F435`-matching targets to `F435_TARGETS`, sets
        `FEATURES += VCP ONBOARDFLASH`, and lists the common `TARGET_SRC` sensor globs.
      - [x] `AT32_UNIFIED.nomk` — empty marker preventing direct build of the base folder
        (mirrors `STM32_UNIFIED.nomk`).
- [x] [make/mcu/AT32F4.mk](make/mcu/AT32F4.mk): removed `drivers/io_at32.c` (folded into shared
      `io.c` instead — see section 3) and `drivers/rcc.c` (was an accidental duplicate — `rcc.c`
      is already unconditionally in [make/source.mk](make/source.mk)'s top-level `COMMON_SRC`,
      same as `system.c`/`io.c`/`exti.c` — always check that list before adding a driver file to
      an MCU-specific `.mk`, to avoid double-compiling the same source into one target).
- Note: `AT32F435` intentionally NOT added to `UNIFIED_TARGETS`/`CI_TARGETS` in
  [make/targets_list.mk](make/targets_list.mk) yet — it can't compile until section 3's
  driver files exist, and adding it to CI now would break CI builds.
- [x] [src/main/common/platform.h](src/main/common/platform.h): added an
      `#elif defined(AT32F435xx)` branch (chip-select header, `FunctionalState`/`ENABLE`/
      `DISABLE` compat typedef since AT-BSP only has `confirm_state`/`TRUE`/`FALSE`, `U_ID_0-2`
      from `0x1FFFF7E8`, confirmed against `betaflight/src/platform/AT32/include/platform/platform.h`).
      NOTE: this is the *only* compat shim added so far (just enough for the files in section 3
      done below). Later driver files will likely need more type aliases (`GPIO_TypeDef` etc.,
      see the betaflight reference file above for the pattern to follow) — add them here as needed.
- [x] [src/main/drivers/nvic.h](src/main/drivers/nvic.h): added an `AT32F43x` branch for
      `NVIC_PRIORITY_GROUPING`/`NVIC_BUILD_PRIORITY` — AT-BSP's `NVIC_PRIORITY_GROUP_2` uses the
      same raw-PRIGROUP encoding as the HAL's `NVIC_PRIORITYGROUP_2` (unlike StdPeriph's
      pre-shifted `NVIC_PriorityGroup_2`), so it reuses the HAL branch's math, just with AT-BSP's
      macro name.

## 3. Driver port (BF4.3-era interfaces, NOT modern Betaflight APIs)

Port logic/reference from `betaflight/src/platform/AT32/*`, reimplemented against
Wingflight's current driver contracts (`timerHardware[]`, `ioTag_t`, `dmaMap`, etc.):

- [~] Clock tree / RCC-CRM (peripheral clock-enable/reset) — **in progress**. Chose the
      additive approach (branch inside the existing shared files) rather than separate
      AT32-only files, since Wingflight's `rcc.c`/`rcc.h`/`rcc_types.h` already have an
      established per-MCU-family `#if/#elif` branching pattern:
      - [x] [src/main/drivers/rcc_types.h](src/main/drivers/rcc_types.h): `rccPeriphTag_t` is
        `uint32_t` for `AT32F43x` (holds the vendor `crm_periph_clock_type` enum value directly).
      - [x] [src/main/drivers/rcc.h](src/main/drivers/rcc.h): `RCC_AHB1/APB1/APB2(periph)` macros
        redefined for `AT32F43x` to expand to `CRM_<periph>_PERIPH_CLOCK` directly (note: AT32
        timers are named `TMRn`, not `TIMn` — callers must use e.g. `RCC_APB2(TMR1)`).
      - [x] [src/main/drivers/rcc.c](src/main/drivers/rcc.c): `RCC_ClockCmd()`/`RCC_ResetCmd()`
        now have a full `#if defined(AT32F43x) ... #else ... #endif` branch calling
        `crm_periph_clock_enable()`/`crm_periph_reset()`. Reset-register offset derived from
        vendor header (`-0x20 << 16` from the clock enum, matching bit position), with the
        ADC1/2/3-share-one-reset-bit exception handled explicitly. STM32 code paths are
        untouched and fully excluded from AT32 builds (verified no stray/unreachable code
        referencing STM32-only symbols remains).
      - [x] System/clock-tree bring-up is now DONE (not build-tested):
        - [x] [src/main/startup/system_at32f435_437.c](src/main/startup/system_at32f435_437.c) —
          vendor CMSIS `SystemInit()`/`system_core_clock_update()`, ported from Artery's/betaflight's
          template almost verbatim (resets CRM to default HICK state; does NOT configure HSE/PLL).
        - [x] [src/main/startup/at32f435_437_clock.c](src/main/startup/at32f435_437_clock.c) +
          `.h` — `system_clock_config()`, the actual HSE→PLL bring-up (288MHz core / 48MHz USB).
          **LIMITATION:** PLL multiplier/divider values are hardcoded for an 8MHz HEXT crystal only
          (matches the reference board) — there's a `#if HSE_VALUE != 8000000 #error` guard so a
          build for a different crystal fails loudly at compile time rather than silently
          misclocking. Unlike STM32, this does NOT yet dynamically compute PLL params for
          arbitrary `HSE_VALUE` — follow-up work if other-crystal boards are needed.
        - [x] [src/main/startup/at32f435_437_conf.h](src/main/startup/at32f435_437_conf.h) — new
          AT-BSP module-enable/include header (adapted from Artery's `_conf_template.h`), needed
          because `at32f435_437.h` unconditionally `#include`s this project-owned filename at the
          bottom. `HEXT_VALUE` is tied to the build's `HSE_VALUE` define.
        - [x] [src/main/drivers/system_at32f43x.c](src/main/drivers/system_at32f43x.c) — driver-layer
          `systemInit()`/`systemResetHard()`/`isMPUSoftReset()`/`enableGPIOPowerUsageAndNoiseReductions()`/
          `checkForBootLoaderRequest()`, adapted from betaflight's AT32 reference. Calls
          `system_clock_config()`. NOTE: unlike the F4 target (which calls
          `checkForBootLoaderRequest()` from the startup `.s` file), this calls it from within
          `systemInit()` (F7-style) — when the AT32 startup `.s` file is eventually written, it
          must NOT also call it, or make sure double-invocation stays harmless.
        - [x] [src/main/drivers/system.c](src/main/drivers/system.c) — added an `AT32F43x` branch
          to the shared `cycleCounterInit()` (uses `system_core_clock` directly instead of
          `HAL_RCC_GetSysClockFreq()`/`RCC_GetClocksFreq()`).
        - [x] [src/main/drivers/persistent.c](src/main/drivers/persistent.c) — added an
          `AT32F43x` branch (`ertc_bpr_data_read/write`, `ertc_write_protect_*`,
          `pwc_battery_powered_domain_access`) plus an AT32 `wasSoftReset` check in
          `persistentObjectInit()` using `crm_flag_get(CRM_SW_RESET_FLAG)`.
      - [ ] Not yet compiled/tested — no toolchain build attempted (build system isn't fully
        wired for a real build yet; most other driver files below don't exist, and the AT32
        startup assembly file + linker script still need to be written/ported).
- [x] GPIO / IO — folded into shared [src/main/drivers/io.c](src/main/drivers/io.c) as an
      `#elif defined(AT32F43x)` branch (following the rcc.c/system.c/persistent.c convention),
      NOT a separate `io_at32.c` (which betaflight has but Wingflight's older BF4.3-era `io.c`
      already branches per-MCU-family inline, so a separate file isn't the established pattern
      here). Added: `ioPortDefs[]` (GPIOA-H via `RCC_AHB1(GPIOx)`), `IORead`/`IOWrite`/`IOHi`/
      `IOLo`/`IOToggle` (AT-BSP `gpio_type` registers: `idt`/`odt`/`scr`/`clr` — `scr` is the
      set/clear register acting like STM32's BSRR, `clr` acts like BRR), `IOConfigGPIO`/
      `IOConfigGPIOAF` (`gpio_init_type` + `gpio_init()` + `gpio_pin_mux_config()`), and added
      `AT32F43x` to the existing `IO_EXTI_Line` `#if` chain. `IO_GPIOPortIdx`/`IO_GPIOPinIdx`
      needed NO changes — they're already MCU-generic (based on `GPIOA_BASE` pointer arithmetic,
      and AT32's `GPIOx_BASE` addresses are spaced 0x400 apart same as STM32).
      Also added: [src/main/common/platform.h](src/main/common/platform.h) now has
      `#define GPIO_TypeDef gpio_type` (confirmed exact same alias pattern used by betaflight's
      own `src/platform/AT32/include/platform/platform.h`) — this is the first of the
      "more type aliases will likely be needed" compat shims predicted earlier; add more here
      (`TIM_TypeDef`→`tmr_type`, `DMA_TypeDef`, `ADC_TypeDef`, etc.) as each driver file needs them.
      Also fixed: [src/main/drivers/nvic.h](src/main/drivers/nvic.h) needed its own `AT32F43x`
      branch too (found while wiring `systemInit()`'s `nvic_priority_group_config()` call) — see
      section 3's clock/system bring-up notes above.
- [x] EXTI — folded into shared [src/main/drivers/exti.c](src/main/drivers/exti.c) as an
      `#elif defined(AT32F43x)` branch throughout (same convention as the GPIO/IO port above),
      NOT a separate `exti_at32.c`. Added: `extiGroupIRQn[]` (AT-BSP's IRQn enum uses `EXINTn_IRQn`
      naming, not `EXTIn_IRQn`), `triggerLookupTable[]` (`EXINT_TRIGGER_RISING/FALLING/BOTH_EDGE`),
      `EXTI_REG_IMR`/`EXTI_REG_PR` (`EXINT->inten`/`EXINT->intsts` — AT-BSP's EXINT peripheral is
      the CRM-clocked equivalent of STM32's EXTI+SYSCFG combined), `EXTIInit()` (enables
      `CRM_SCFG_PERIPH_CLOCK` — AT32's SCFG peripheral does the GPIO-port-to-EXINT-line muxing,
      equivalent to STM32's SYSCFG), `EXTIConfig()` (`scfg_exint_line_config()` for the port/pin
      mux + `exint_init_type`/`exint_default_para_init()`/`exint_init()` for the line config,
      `nvic_irq_enable()` for the IRQ enable/priority — note AT-BSP combines NVIC priority+enable
      into one call, unlike STM32's separate `NVIC_Init()`), `EXTIEnable`/`EXTIDisable`. The
      generated `_EXTI_IRQ_HANDLER(...)` invocations at the bottom of the file are now wrapped in
      an `#if defined(AT32F43x) ... #else ... #endif` since the vector-table symbol names differ
      (`EXINT0_IRQHandler` etc., confirmed against betaflight's own
      `startup_at32f435_437.s` weak-alias list) — the future Wingflight AT32 startup `.s` file
      MUST use these exact `EXINTn_IRQHandler`/`EXINT9_5_IRQHandler`/`EXINT15_10_IRQHandler` names
      in its vector table, not `EXTIn_IRQHandler`.
      Also removed `drivers/exti_at32.c` from `make/mcu/AT32F4.mk` (superseded by the additive
      approach, and `drivers/exti.c` is already unconditionally in `make/source.mk`'s
      `COMMON_SRC` anyway — would have been a duplicate like the `rcc.c` one found earlier).
- [x] DMA + request map — **NEW FINDING**: unlike GPIO/EXTI, DMA's descriptor/IRQ-handler
      layer is NOT folded into a shared file across MCU families — Wingflight already keeps
      that part in separate per-MCU files (`dma_stm32f4xx.c`, `dma_stm32f7xx.c`,
      `dma_stm32g4xx.c`, `dma_stm32h7xx.c`), while only `dma.h`/`dma_common.c`/
      `dma_reqmap.c`/`dma_reqmap.h` are shared with `#if/#elif` branches. AT32 follows the
      same split:
      - [x] [src/main/drivers/dma.h](src/main/drivers/dma.h): added an `AT32F43x` branch —
        `DMA_ARCH_TYPE` = `dma_channel_type`; `dmaChannelDescriptor_t` gained a new opt-in
        `dmamux_channel_type *dmamux` field (AT32-only, for DMAMUX request-ID routing, no
        equivalent concept existed before); a dedicated top-level `dmaIdentifier_e` branch
        (14 channels: `DMA1_CH1..7_HANDLER`, `DMA2_CH1..7_HANDLER`) with its own
        `DEFINE_DMA_CHANNEL`/`DEFINE_DMA_IRQ_HANDLER` macros — **could not reuse** the
        existing default/STM32G4 macros because AT-BSP's peripheral-pointer macros are
        all-caps (`DMA1_CHANNEL1`) while the CMSIS IRQn enum is mixed-case
        (`DMA1_Channel1_IRQn`); `DMA_CLEAR_FLAG`/`DMA_GET_FLAG_STATUS` use AT32's per-channel
        `sts`/`clr` registers (4 bits/channel, same `TCIF=0x02`/`HTIF=0x04`/`TEIF=0x08`
        layout as the default STM32F1/F3 branch, reused as-is); `IS_DMA_ENABLED` checks
        `ctrl & 0x1`; added `dmaMuxEnable(dmaIdentifier_e, uint32_t dmaMuxId)` declaration.
      - [x] [src/main/common/platform.h](src/main/common/platform.h): added
        `#define DMA_TypeDef dma_type` and (needed for `dma_reqmap.c`'s timer table)
        `#define TIM_TypeDef tmr_type` compat aliases.
      - [x] [src/main/drivers/dma_reqmap.h](src/main/drivers/dma_reqmap.h): added
        `AT32F43x` to the `dmaChannelSpec_t.channel` field guard (so it stores a DMAMUX
        request-ID like STM32H7/G4 do, rather than a per-option static channelSpec array);
        `MAX_PERIPHERAL_DMA_OPTIONS`/`MAX_TIMER_DMA_OPTIONS` = `14` (one distinct value per
        real DMA1/DMA2 channel, since AT32's DMAMUX makes every channel freely assignable).
      - [x] [src/main/drivers/dma_reqmap.c](src/main/drivers/dma_reqmap.c): added an
        `#elif defined(AT32F43x)` mapping-table branch (mirrors STM32G4's single
        `dmaRequest` field structure) resolving directly to `DMAMUX_DMAREQ_ID_*` vendor
        constants — SPI1-4 MOSI/MISO, ADC1-3 (AT32F435 only has 3 ADCs, no ADC4/5),
        UART1-5 (NOT UART6-8, matching STM32G4's scope), TIMUP for TMR1/2/3/4/5/8/20 (using
        AT32's `_OVERFLOW` suffix instead of STM32's `_UP`; **TMR15/16/17 are intentionally
        excluded** — confirmed absent from AT32F435's DMAMUX request-ID enum, unlike G4),
        and TMR1/2/3/4/5/8/20 CH1-4 (all real GP/advanced timer channels present in the
        DMAMUX enum). Also added a `dmaChannelSpec[MAX_PERIPHERAL_DMA_OPTIONS]` static pool
        covering DMA1/DMA2 channels 1-7 using AT-BSP's all-caps `DMA ## d ## _CHANNEL ## c`
        peripheral macros. Extended the 5 shared `#if defined(STM32H7) || defined(STM32G4)`
        runtime guards in the common tail functions
        (`dmaSetupRequest`/`dmaGetChannelSpecByPeripheral`/`dmaGetChannelSpecByTimerValue`/
        `dmaGetOptionByTimer`/`dmaGetUpOptionByTimer`) to include `AT32F43x`.
      - [x] [src/main/drivers/dma_at32f43x.c](src/main/drivers/dma_at32f43x.c) — **new file**
        (per-MCU descriptor/IRQ layer, mirrors `dma_stm32f4xx.c`'s role; already referenced
        by `AT32F4.mk`). `dmaDescriptors[]` via `DEFINE_DMA_CHANNEL(DMA1/DMA2, 1-7, shift)`
        with `flagsShift` 0/4/8/12/16/20/24 (both controllers restart at 0 — separate
        sts/clr registers, unlike STM32's single shared register per controller);
        `DEFINE_DMA_IRQ_HANDLER` for all 14 channels; `dmaEnable()`/`dmaSetHandler()` using
        `RCC_ClockCmd(RCC_AHB1(DMA1/DMA2), ENABLE)` + `nvic_irq_enable()` (matches the
        pattern already established in `exti.c` for AT32 — no HAL/StdPeriph NVIC API used);
        new `dmaMuxEnable()` calling AT-BSP's `dma_flexible_config()` to bind a
        `DMAMUX_DMAREQ_ID_*` value to a channel's DMAMUX.
      - **CORRECTED next session**: the original claim here (that AT32 doesn't need
        `xDMA_*` translation shims at all) was wrong in an important nuance. AT32's own
        `*_at32bsp.c` consumer files (e.g. the new `bus_spi_at32bsp.c` below) ARE written in
        the StdPeriph-shim style and DO call `xDMA_Init`/`xDMA_Cmd`/`xDMA_ITConfig`/etc. —
        confirmed via betaflight's own `platform/AT32/include/platform/dma.h`, which defines
        an AT32-specific redefinition of these exact macro names pointing at AT-BSP's
        `dma_init()`/`dma_channel_enable()`/`dma_interrupt_enable()`/etc. `dma.h` now has
        this `#elif defined(AT32F43x)` branch (excluding `xDMA_GetFlagStatus`/`xDMA_ClearFlag`/
        `xDMA_MemoryTargetConfig`, which are provably unused anywhere and have an incompatible
        1-arg-vs-2-arg AT-BSP signature — use `DMA_GET_FLAG_STATUS`/`DMA_CLEAR_FLAG` instead).
        Lesson: a macro being dead for the OLD platform doesn't prove the NEW platform's own
        consumer files won't reintroduce the same macro NAME pointing at a different impl.
      - [x] Compiles-by-inspection only — no toolchain build attempted yet.
- [x] SPI bus (`bus_spi_at32bsp`) — new [src/main/drivers/bus_spi_at32bsp.c](src/main/drivers/bus_spi_at32bsp.c),
      mirrors `bus_spi_stdperiph.c`'s function signatures using AT-BSP calls
      (`spi_i2s_reset/init/enable`, `spi_i2s_dma_transmitter/receiver_enable`,
      `spi_i2s_data_transmit/receive`, `spi_i2s_flag_get`, `dma_default_para_init` via the new
      `DMA_InitTypeDef`→`dma_init_type` platform.h alias, `xDMA_*` macros for DMA start/stop).
      Also required fixes to get here: [src/main/common/platform.h](src/main/common/platform.h)
      gained `SPI_TypeDef`→`spi_type`/`DMA_InitTypeDef`→`dma_init_type` aliases;
      [src/main/drivers/bus_spi_impl.h](src/main/drivers/bus_spi_impl.h) extended its
      per-pin-AF guards (`MAX_SPI_PIN_SEL`=4, `spiPinDef_t.af`, `SPIDevice_s` per-pin AF,
      `spiHardware_t.af`-exclusion) to include `AT32F43x` (AT32 uses per-pin GPIO MUX 0-15,
      like F7/H7/G4, not per-peripheral AF like F4); **[src/main/drivers/io.h](src/main/drivers/io.h)
      had a pre-existing gap** — no `AT32F43x` branch existed for `IO_CONFIG`/`IOCFG_*` macros
      or the `IOConfigGPIOAF` prototype guard at all, even though `io.c`'s AT32
      `IOConfigGPIO`/`IOConfigGPIOAF` (from a prior session) already assumed the F4-style
      4-field cfg bit layout — fixed by adding the missing branch (lesson: a prior "io.c full
      AT32 port" note doesn't guarantee the paired header macros were also updated — always
      re-check); [src/main/drivers/bus_spi.h](src/main/drivers/bus_spi.h) gained an `AT32F43x`
      branch for `SPI_IO_AF_CFG`/`SPI_IO_AF_SCK_CFG`/`SPI_IO_AF_MISO_CFG`/`SPI_IO_CS_CFG`;
      [src/main/drivers/bus_spi.c](src/main/drivers/bus_spi.c) gained an `AT32F43x` branch in
      `spiCalculateDivider`/`spiCalculateClock` (using `system_core_clock`) plus explicit
      `dmaMuxEnable()` calls in `spiInitBusDMA()` after each `dmaEnable()` — **a deliberate
      deviation from upstream betaflight**, whose own AT32 port never calls `dmaMuxEnable()`
      for SPI/UART DMA (looks like a genuine upstream gap, only ADC/dshot/ws2811 call it).
  - [x] [src/main/drivers/bus_spi_pinconfig.c](src/main/drivers/bus_spi_pinconfig.c)'s
        `spiHardware[]` table gained a real AT32F435 SCK/MISO/MOSI pin → `GPIO_MUX_x` AF
        mapping block. Previously believed blocked ("no reference exists"), but betaflight's
        own vendored AT32 port (`betaflight/src/platform/common/stm32/bus_spi_pinconfig.c`'s
        `#ifdef AT32F4` block) already contains this exact real, datasheet-derived per-pin
        table — reused verbatim rather than fabricated. That upstream table needs 5 pin-select
        slots for SPI3 MOSI, so `MAX_SPI_PIN_SEL` for `AT32F43x` was bumped from 4 to 5 in
        [src/main/drivers/bus_spi_impl.h](src/main/drivers/bus_spi_impl.h) (matching
        betaflight's own `platform/AT32/include/platform/platform.h`) instead of truncating
        real pin data. Build-verified: `make TARGET=AT32F435 -j8` → `EXITCODE=0`, zero warnings.
- [x] I2C bus (`bus_i2c_atbsp*`) — new [src/main/drivers/bus_i2c_at32bsp.c](src/main/drivers/bus_i2c_at32bsp.c)
      (single file, combining what betaflight splits into `bus_i2c_atbsp.c`+`bus_i2c_atbsp_init.c`),
      built on AT-BSP's higher-level blocking/interrupt **middleware** driver
      (`lib/main/AT32F43x/middlewares/i2c_application_library/i2c_application.c`, a separate
      layer above the raw register driver `at32f435_437_i2c.c` — provides `i2c_handle_type`,
      `i2c_master_transmit/receive`, `i2c_memory_write/read[_int]`, `i2c_wait_flag`, `i2c_config`)
      rather than betaflight's newer `i2cHalHandle_t` wrapper abstraction. Real AT32F435 I2C1-3
      SCL/SDA pin+`GPIO_MUX_x` data was ported directly from betaflight's own (working) AT32 port
      `bus_i2c_atbsp_init.c` — a legitimate hardware data source (unlike the still-blocked SPI
      pinconfig table). Dependency fixes required: [src/main/common/platform.h](src/main/common/platform.h)
      gained `I2C_TypeDef`→`i2c_type` alias; [src/main/drivers/bus_i2c_impl.h](src/main/drivers/bus_i2c_impl.h)
      extended its per-pin-AF guards (`i2cPinDef_t.af`, `I2CPINDEF` macro, `i2cDevice_s`
      `sclAF`/`sdaAF`) to include `AT32F43x`, bumped `I2C_PIN_SEL_MAX` to 8 for AT32 only (I2C2 has
      up to 8 alternate SDA pins — more than the STM32 families' max of 4), added an
      `i2c_handle_type handle;` member to `i2cDevice_t` (AT32's per-device middleware handle,
      parallel to F4's `i2cState_t state`/HAL's `I2C_HandleTypeDef handle`), and now includes
      `i2c_application.h` under an `AT32F43x` guard; [src/main/drivers/bus_i2c_config.c](src/main/drivers/bus_i2c_config.c)
      extended its two `sclAF`/`sdaAF` assignment guards to include `AT32F43x`;
      [src/main/drivers/bus_i2c.h](src/main/drivers/bus_i2c.h) gained an `AT32F43x` branch for
      `I2CDEV_COUNT`=3 (AT32F435/437 has exactly 3 physical I2C peripherals, confirmed via vendor
      `crm_periph_reset` — no I2C4); [make/mcu/AT32F4.mk](make/mcu/AT32F4.mk) gained the
      `i2c_application_library` dir in `VPATH` and `i2c_application.c` added to `MCU_COMMON_SRC`
      (the include dir was already present from earlier scaffolding, but the source file itself
      was never wired into the build — caught this before it became a link error).
      Verified: I2C IRQn naming is `I2C1_EVT_IRQn`/`I2C1_ERR_IRQn` (not `_EV_`/`_ER_` like F4) —
      confirmed against the vendored CMSIS device header; `i2c_init()` internally disables the
      peripheral before writing `clkctrl`, so calling `i2c_config()` (which resets+enables) before
      `i2c_init()` (matching betaflight's proven call order exactly) is safe.
- [x] UART / serial (`serial_uart_at32bsp`, `serial_uart_at32f43x`) — new
      [src/main/drivers/serial_uart_at32f43x.c](src/main/drivers/serial_uart_at32f43x.c) (hardware
      table, real AT32F435 UART1-8 pin/AF/RCC/DMAMUX-request-ID data ported from betaflight's own
      AT32 port and restructured to Wingflight's older `uartHardware_t` field names — `.device`/
      `.reg`/`.rxDMAChannel`/`.txDMAChannel`/`.irqn` instead of `.identifier`/`.rxDMAMuxId`/
      `.txDMAMuxId`) and
      [src/main/drivers/serial_uart_at32bsp.c](src/main/drivers/serial_uart_at32bsp.c) (logic file,
      modeled on `serial_uart_stdperiph.c`'s simpler non-register-pinswap style since AT32's pin
      tables already resolve swap via distinct GPIO pins, not a peripheral-internal swap register).
      **Found and fixed a real, previously-latent bug**: `USE_DMA_SPEC` was never defined for
      `AT32F43x` anywhere in [src/main/target/common_pre.h](src/main/target/common_pre.h) even
      though `dma_reqmap.c`'s entire AT32 branch (written in an earlier session) is wrapped in
      `#ifdef USE_DMA_SPEC` — meaning that DMA request-ID resolution code was dead/uncompiled this
      whole time. Fixed with a minimal new `#ifdef AT32F43x` block defining only `USE_DMA_SPEC` (not
      the full STM32G4-style feature set — `USE_DSHOT_BITBANG`/`USE_USB_MSC`/`USE_USB_CDC_HID`/
      `USE_MCO`/`USE_TIMER_MGMT` etc. are deliberately deferred until their respective AT32 driver
      files exist, to avoid enabling code paths for files that don't exist yet); also added
      `AT32F43x` to the fast-task-rate (`TASK_GYROPID_DESIRED_PERIOD` 125us/8kHz) MCU group
      alongside F4/F7/H7/G4, since AT32F435 (288MHz) is easily fast enough for it. Confirmed via
      `common_post.h` that `USE_LED_STRIP` (unconditionally defined for adequate flash size) keeps
      `USE_DMA_SPEC` from being `#undef`'d by the `#if defined(USE_DSHOT) || defined(USE_LED_STRIP)`
      guard there, so this minimal fix is sufficient without also needing `USE_DSHOT` yet.
      Other dependency fixes: [src/main/common/platform.h](src/main/common/platform.h) gained a
      `USART_TypeDef`→`usart_type` alias; [src/main/drivers/serial_uart_impl.h](src/main/drivers/serial_uart_impl.h)
      gained an `AT32F43x` branch for `UARTDEV_COUNT_MAX`=8/`UARTHARDWARE_MAX_PINS`=4, was added to
      the per-pin `uartPinDef_t.af` guard, was *excluded* from the legacy single `uartHardware_t.af`
      field guard (like F7 — AT32 uses per-pin AF), was *not* added to the `txIrq`/`rxIrq` guard
      (falls to the plain `irqn` field like F4, since AT32 uses one `nvic_irq_enable()` call like
      the DMA/EXTI/I2C ports already do), and gained a `UART_REG_RXD`/`UART_REG_TXD` branch using
      the vendor-header-confirmed shared `->dt` register field name (AT-BSP uses one data register
      for both TX and RX, unlike STM32's split RDR/TDR or unified DR).
      [src/main/drivers/serial_uart.c](src/main/drivers/serial_uart.c) (shared/MCU-generic) gained:
      an `AT32F43x` branch for `UART_TX_BUFFER_ATTRIBUTE`/`UART_RX_BUFFER_ATTRIBUTE` (empty, like
      F4 — no special DMA-RAM region needed); an `AT32F43x` branch in `uartWrite()`'s non-DMA path
      using `usart_interrupt_enable(uartPort->USARTx, USART_TDBE_INT, ENABLE)` in place of
      `USART_ITConfig(..., USART_IT_TXE, ENABLE)` (no AT-BSP equivalent of that StdPeriph function
      exists — this was the one non-MCU-specific-file StdPeriph call that needed a shim); and,
      matching the SPI DMA precedent, an explicit `dmaMuxEnable(identifier, uartPort->txDMAChannel)`/
      `dmaMuxEnable(identifier, uartPort->rxDMAChannel)` call in `uartConfigureDma()`'s
      `#if defined(AT32F43x)` branches (AT32's DMAMUX channel-to-request routing is a separate step
      from `dmaEnable()`, unlike STM32's fire-and-forget channel selection).
      Verified via vendor header: AT32F435/437 has 8 UART/USART peripherals (USART1-3, UART4-5,
      USART6, UART7-8) with real IRQn values (`USART1_IRQn`=37 ... `UART8_IRQn`=83, confirmed
      distinct from STM32's numbering, so the `UART_IRQHandler` macro-generated handler names
      still work because AT32's CMSIS vector table entries use the identical STM32-style names
      `USARTn_IRQHandler`/`UARTn_IRQHandler`); flag/interrupt constants `USART_RDBF_FLAG`/
      `USART_TDBE_FLAG`/`USART_TDC_FLAG`/`USART_ROERR_FLAG`/`USART_IDLEF_FLAG`/`USART_RDBF_INT`/
      `USART_IDLE_INT`/`USART_TDBE_INT`/`USART_TDC_INT` all confirmed present; `usart_type`'s data
      register field is named `dt` (confirmed, not just inferred from betaflight's reference as in
      the prior session's notes). **Known limitation carried forward**: `dma_reqmap.c`'s AT32
      DMAMUX request-ID resolution table (consumed by the CLI `dma` command via `USE_DMA_SPEC`)
      still only covers UART1-5 (matching STM32G4 parity) — UART6/7/8 have their real
      `DMAMUX_DMAREQ_ID_*` values in the new hardware table (used only if a board hardcodes a DMA
      channel via `UARTn_xx_DMA_CHANNEL` override macros) but won't get CLI-driven "dma" resource
      assignment until `dma_reqmap.c` is extended to cover them too.
- [x] Timers (`timer_at32bsp`, `timer_at32f43x`) — new
      [src/main/drivers/timer_at32bsp.c](src/main/drivers/timer_at32bsp.c) (logic file, full
      AT-BSP port of `drivers/timer.c`'s function surface). `drivers/timer.c` is deeply coupled
      to STM32 StdPeriph API calls and direct STM32 register field names (`tim->SR`/`->DIER`/
      `->CCR1-4`/`->CNT`/`->ARR`/`->EGR`/`->CCER`) and cannot be shimmed incrementally with
      `#elif` branches the way I2C/UART/SPI were — so, exactly mirroring the existing
      `drivers/timer_hal.c` precedent (which replaces `timer.c` for the STM32F7/H7/G4 HAL
      platforms), `timer.c` is now excluded for AT32 via a new `MCU_EXCLUDES` block in
      [make/mcu/AT32F4.mk](make/mcu/AT32F4.mk) and fully replaced by `timer_at32bsp.c`.
      [src/main/drivers/timer_at32f43x.c](src/main/drivers/timer_at32f43x.c) (hardware tables,
      already existed from earlier scaffolding) gained a new `timerClock()` implementation
      (the one function `timer_stm32f4xx.c`/`timer_stm32f7xx.c`/etc. provide per-MCU-family
      alongside their hardware tables) using `crm_clocks_freq_get()` +
      `CRM->cfg_bit.apb1div`/`apb2div` doubling logic, matching betaflight's own
      `timerClockFromInstance()` reference exactly: TMR1/8/9/10/11/20 are APB2, all others
      (TMR2-7, TMR12-14) are APB1.
      **Channel encoding note (important, verified against `timer_def.h`'s existing
      `DEF_TIM_CHANNEL__D` comment)**: Wingflight/STM32 encode `timerHardware_t.channel` as a
      BYTE offset (0/4/8/12 for channels 1-4). AT-BSP's own `tmr_channel_select_type` enum uses
      0/2/4/6 for the same four channels. These convert cleanly via `channel >> 1` — this
      `AT_CH_SELECT()` macro is used everywhere a `tmr_channel_select_type` argument is needed
      (`tmr_output_channel_config`, `tmr_output_channel_buffer_enable`, `tmr_input_channel_init`).
      **Register-level facts verified directly against the vendored
      `lib/main/AT32F43x/drivers/inc/at32f435_437_tmr.h`** (not just inferred from betaflight's
      reference): `tmr_type`'s capture/compare registers are named `c1dt`/`c2dt`/`c3dt`/`c4dt`
      and are laid out 4 bytes apart sequentially (offsets 0x34/0x38/0x3C/0x40) — identical
      spacing to STM32's CCR1-4, so the original STM32 code's raw pointer-arithmetic trick for
      `timerChCCR`/`timerChCCRHi`/`timerChCCRLo` (`(volatile char*)&tim->c1dt + channel`) works
      completely unchanged (this exact trick is already used by the pre-existing, MCU-generic
      `timerCCR()` in [src/main/drivers/timer_common.c](src/main/drivers/timer_common.c), which
      already had a verified `AT32F43x` branch — confirming this approach independently).
      `iden` (interrupt/DMA-request enable register) and `ists` (interrupt status register)
      share identical per-source bit positions (`ovf`=bit0, `c1`=bit1..`c4`=bit4, `hall`=bit5,
      `trg`/`t`=bit6, `brk`=bit7) — exactly like STM32's DIER/SR pairing, so the original
      `__builtin_clz`-based bit-scanning IRQ handler logic (`timCCxHandler`/`timUpdateHandler`)
      ports with only register-name substitution. `cctrl` (CCER equivalent) also has an
      identical bit layout to STM32 (`c1en`/`c1p`/`c1cen`/`c1cp` at bits 0-3, `c2xx` at 4-7,
      `c3xx` at 8-11, `c4en`/`c4p` at 12-13 with no complementary bits for channel 4), so
      `timerChICPolarity`'s raw polarity-bit-flip trick (channel value used directly as a shift
      amount) also ports unchanged. `TMR_Cx_FLAG` and `TMR_Cx_INT` constants share identical bit
      values per channel, so a single `TIM_IT_CCx()` macro serves both flag-clear and
      interrupt-enable calls, matching the original code's dual-purpose `TIM_IT_CCx`.
      **IRQn/IRQHandler names verified against the vendored CMSIS startup file**
      (`lib/main/AT32F43x/cmsis/cm4/device_support/startup/gcc/startup_at32f435_437.s`, not
      guessed): `TMR1_CH_IRQHandler`, `TMR1_BRK_TMR9_IRQHandler`, `TMR1_OVF_TMR10_IRQHandler`,
      `TMR1_TRG_HALL_TMR11_IRQHandler`, `TMR2_GLOBAL_IRQHandler`..`TMR5_GLOBAL_IRQHandler`,
      `TMR6_DAC_GLOBAL_IRQHandler` (TMR6 shares its vector with DAC on AT32F435/437 — noted as a
      future check if/when `USE_DAC` is ever added), `TMR7_GLOBAL_IRQHandler`,
      `TMR8_CH_IRQHandler`, `TMR8_BRK_TMR12_IRQHandler`, `TMR8_OVF_TMR13_IRQHandler`,
      `TMR8_TRG_HALL_TMR14_IRQHandler`, `TMR20_CH_IRQHandler` — all confirmed to exist as real
      weak vector-table entries. Mirrored the original code's IRQ-handler-generation asymmetry
      exactly: only the two "overflow"-shared vectors (`TMR1_OVF_TMR10`, `TMR8_OVF_TMR13`) get
      the dual-timer-check (`_TIM_IRQ_HANDLER2`) treatment when both timers in the pair are used;
      the BRK/TRG_HALL-shared vectors (TMR9/11/12/14) always get a single-timer handler, matching
      the original STM32 code's own asymmetric precedent (it only special-cased the analogous
      `TIM1_UP_TIM10`/`TIM8_UP_TIM13` vectors, not `TIM1_BRK_TIM9`/`TIM1_TRG_COM_TIM11`/
      `TIM8_BRK_TIM12`/`TIM8_TRG_COM_TIM14`).
      **`timerChConfigOC` mode mapping (documented assumption, not found in any reference file —
      betaflight's own newer AT32 port moved output-compare configuration out of
      `timer_at32bsp.c` entirely, so there was no direct example to copy)**: mapped STM32's
      `TIM_OCMode_Inactive` (forces OCxREF low pre-polarity, used with `outEnable=true` to drive a
      static level) to AT-BSP's `TMR_OUTPUT_CONTROL_FORCE_LOW`, and STM32's `TIM_OCMode_Timing`
      (no effect on the output pin at all, used with `outEnable=false` for measurement-only
      channels) to AT-BSP's `TMR_OUTPUT_CONTROL_OFF`. This is a reasoned mapping based on the
      AT-BSP header's own doc comments, not verified against real hardware or the AT32 reference
      manual — flagged here for extra scrutiny once PWM/DSHOT output bring-up is attempted.
      **`common_pre.h` also gained `#define USE_TIMER_MGMT` for `AT32F43x`** (previously
      deliberately deferred, per the prior UART-session's TODO note, until the timer driver
      files existed). This was a real, necessary fix, not just an enhancement: per
      `common_post.h`'s `#if defined(USE_TIMER_MGMT) ... #else #undef USE_UNIFIED_TARGET
      #endif` logic, `USE_UNIFIED_TARGET` — required by this project's `AT32_UNIFIED` unified
      build architecture — would otherwise have been silently undefined for every AT32 build.
      `platform.h` also gained a `TIM_OCInitTypeDef`→`tmr_output_config_type` alias (needed for
      `timer.h`'s `timerOCInit()` declaration to compile at all); other shared files that build a
      `TIM_OCInitTypeDef` using STM32 field names directly (`pwm_output.c`, `pwm_output_dshot.c`,
      `dshot_dpwm.h`, `light_ws2811strip_stdperiph.c`, `dshot_bitbang_stdperiph.c`) are NOT yet
      portable to AT32 and are expected to be replaced by `_at32bsp.c` equivalents in the
      PWM/DSHOT and WS2811 port phases still to come — this is a known, pre-existing,
      already-tracked gap, not a regression introduced here.
      **Known limitation carried forward**: `timerReconfigureTimeBase()`/`timerStart()` are
      implemented (the former as a thin wrapper over `configTimeBase`; the latter as a no-op,
      matching the original STM32 `timer.c`'s own dead-code `#if 0` body) but not exercised by
      anything yet since PWM/DSHOT/ELRS consumers aren't ported for AT32 yet.
- [x] ADC (`adc_at32f43x`) — new file
      [src/main/drivers/adc_at32f43x.c](src/main/drivers/adc_at32f43x.c). Follows
      `drivers/adc_stm32f4xx.c`'s single-selected-ADC-device architecture (`config->device`
      picks ONE of ADC1/2/3, every enabled channel — vbat/current/rssi/vbec/vbus/vext — is
      wired to that same device) since Wingflight's own `adc.h`/`adc_impl.h`/`pg/adc.h` are
      F4/F7-vintage, NOT the newer per-channel-multi-device model used by betaflight's own
      current AT32 ADC port (incompatible without a header rewrite this project explicitly
      wants to avoid). `adcHardware[]` has all 3 ADCs (`RCC_APB2(ADC1/2/3)`, no
      `.dmaResource`/`.channel` fields since `USE_DMA_SPEC` is always defined for AT32).
      `adcTagMap[]` has all 16 ADC-capable pins (PA0-7/PB0-1/PC0-5) with real
      `ADC_DEVICES_123`/`ADC_DEVICES_12` availability masks, sourced from betaflight's own
      AT32 reference file (legitimate hardware data, reused even though that file's overall
      architecture wasn't). Uses AT-BSP's `adc_base_config`/`adc_common_config` (called once,
      shared across all 3 ADCs)/`adc_resolution_set`/oversampling
      (`adc_ordinary_oversample_enable` + `ADC_OVERSAMPLE_RATIO_4`/`_SHIFT_2`, an accuracy
      improvement AT-BSP supports that StdPeriph doesn't)/`adc_ordinary_channel_set` per
      channel/DMA via `dma_init_type`+`dmaGetChannelSpecByPeripheral(DMA_PERIPH_ADC, ...)`.
      **Needed an intermediate conversion buffer**: unlike STM32 StdPeriph where DMA can write
      directly into the shared `uint16_t adcValues[]`, AT-BSP's `odt` register is transferred a
      full 32-bit word at a time (per betaflight's own working AT32 reference), so a
      `static volatile DMA_DATA uint32_t adcConversionBuffer[ADC_CHANNEL_COUNT]` is used as the
      DMA target, with `adcGetChannelValues()` narrowing each enabled channel's value into
      `adcValues[]` (mirrors the same technique `drivers/adc_stm32g4xx.c` already uses natively
      for its own, different reason). AT-BSP also requires a mandatory ADC calibration sequence
      before first use (`adc_calibration_init()`/`_start()` + status-poll waits) that has no
      StdPeriph equivalent — included, matching the vendor's documented enable procedure.
      `USE_ADC_INTERNAL` (temp sensor/vrefint via injected/preempt channels) — **now ported, see
      below** (was NOT enabled for AT32 as of this paragraph's original writing).
      **Found and fixed two more real latent gaps while researching this** (same class as the
      UART/Timer sessions' `USE_DMA_SPEC`/`USE_TIMER_MGMT` bugs):
      1. `dma_reqmap.c`'s existing AT32 `REQMAP(ADC, 1/2/3)` entries (written in an earlier
         session) expand to `DMA_REQUEST_ADC1/2/3`, but no such macro existed anywhere — the
         vendor enum names are `DMAMUX_DMAREQ_ID_ADC1/2/3` (different prefix). Would have been
         an undefined-identifier compile error the moment `USE_ADC` code was actually built
         (which it always is — `AT32_UNIFIED/target.h` already defines `USE_ADC`). Fixed by
         adding explicit `#define DMA_REQUEST_ADC1 DMAMUX_DMAREQ_ID_ADC1` (and `_ADC2`/`_ADC3`)
         resolution aliases, matching the existing UART/SPI alias-block convention in the same
         file.
      2. While verifying the ADC fix, found the SAME class of bug already latent for Timers:
         `REQMAP_TIMUP(TIMUP, n)` expands to `DMA_REQUEST_TMRn_OVERFLOW`, but only `_CHx`
         aliases existed (from the Timer-port session), never `_OVERFLOW` ones — for
         TMR1/2/3/4/5/8/20. Fixed by adding the 7 missing `#define DMA_REQUEST_TMRn_OVERFLOW
         DMAMUX_DMAREQ_ID_TMRn_OVERFLOW` aliases. This was a real gap that would have broken
         the CLI `dma` command's TIMUP resolution for AT32 once `USE_TIMER`-gated code paths
         were exercised.
      **`platform.h`** gained a `ADC_TypeDef`→`adc_type` alias (needed for `adc.h`/`adc_impl.h`'s
      `ADC_TypeDef *instance` parameters to compile).
      **`adc_impl.h`** gained an `#elif defined(AT32F43x)` branch for `ADC_TAG_MAP_COUNT` = 16
      (previously would have fallen into the generic `else: 10` branch, a size mismatch against
      the real 16-entry `adcTagMap[]` that would have under-sized the `ARRAYLEN()`-driven pin
      lookup in `adc.c`).
      **`common_pre.h`** gained `defined(AT32F43x)` in the `DMA_DATA`/`DMA_DATA_ZERO_INIT`/
      `STATIC_DMA_DATA_AUTO` macro's `#if defined(STM32F4) || defined(STM32G4)` condition —
      AT32 was previously falling into the `#else` branch, which references a
      `.dmaram_data`/`.dmaram_bss` linker section that doesn't exist for AT32 (no linker script
      written yet) — would have been a link error the moment any `DMA_DATA`-attributed variable
      (like the new `adcConversionBuffer`) was actually linked. AT32 now gets the same
      "no special DMA-RAM region needed" treatment already established for F4/G4 (and matching
      the choice already made for AT32's UART TX/RX buffer attributes).
      **`make/mcu/AT32F4.mk`** needed NO changes — `adc_at32f43x.c` was already listed in
      `MCU_COMMON_SRC` from earlier scaffolding.
- [x] Persistent storage — done above, folded into `drivers/persistent.c` (not a separate
      `persistent_at32bsp.c` file — kept the additive-branch convention).
- [x] PWM output + DSHOT (`pwm_output.c` additive branch + new `pwm_output_dshot_at32bsp.c`).
      Split into two pieces after inspecting actual function bodies (refining the earlier
      "timer.c precedent" assumption that shared driver files with ANY StdPeriph-specific code
      always need a full parallel `_at32bsp.c` file — that's only true for files with deep,
      pervasive coupling; small/localized StdPeriph usage is better handled additively):
      1. **`drivers/pwm_output.c`** (plain PWM/oneshot/multishot/servo/camera-control/beeper
         outputs) only had 2 functions touching StdPeriph directly
         (`pwmOCConfig()`/`pwmOutConfig()`), so it got a small additive `#elif defined(AT32F43x)`
         branch in each instead of a separate file — no new file needed. Builds a native
         `TIM_OCInitTypeDef` (=`tmr_output_config_type`) with `oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A`,
         `oc_output_state`/`oc_idle_state`/`oc_polarity` (or the `occ_*` complementary-channel
         equivalents for `TIMER_OUTPUT_N_CHANNEL`), then `timerOCInit()` + `timerOCPreloadConfig()`
         (both already implemented in `timer_at32bsp.c`) + `tmr_channel_value_set()` for the
         actual compare value (AT-BSP's OC struct has **no pulse/CCR field**, unlike StdPeriph's
         `TIM_OCInitTypeDef.TIM_Pulse` — the compare value must always be set separately).
         `pwmOutConfig()`'s AT32 branch uses `tmr_output_enable()` + `tmr_counter_enable()` in
         place of `TIM_CtrlPWMOutputs()`/`TIM_Cmd()`.
      2. **`drivers/pwm_output_dshot.c`** (classic DMA-to-timer-CCR DSHOT) DOES have deep,
         pervasive StdPeriph coupling with no AT-BSP equivalent (`TIM_CCxNCmd`/`TIM_CCxCmd`,
         `TIM_TimeBaseInitTypeDef`/`TIM_ICInit`/`TIM_DMACmd`/`TIM_SetCounter`/`TIM_DMAConfig`/
         `TIM_DMABase_CCR1`/`TIM_DMABurstLength_4Transfers`, direct `->DMAR`/`->ARR` register
         access) — same class of problem as `timer.c`, so it got a full native replacement file,
         [src/main/drivers/pwm_output_dshot_at32bsp.c](src/main/drivers/pwm_output_dshot_at32bsp.c),
         mutually exclusive with the original via simply not listing `pwm_output_dshot.c` in
         `AT32F4.mk`'s `MCU_COMMON_SRC` (no `MCU_EXCLUDES` entry needed for this one, unlike
         `timer.c` — `pwm_output_dshot.c` was never in the global `source.mk` `COMMON_SRC` to
         begin with, it's only ever referenced per-MCU).
         **Initial pass** (documented in the new file's header comment and in `common_pre.h`):
         no `USE_DSHOT_DMAR` (4-channel timer-update-event burst DMA via STM32's `->DMAR` alias
         register — AT-BSP timers have no equivalent alias register; see the dedicated
         `USE_DSHOT_TELEMETRY`/`USE_DSHOT_DMAR` section below for why this is now a deliberate
         permanent decision, not just a deferral) and no `USE_DSHOT_TELEMETRY`/bidirectional
         DSHOT (needs the timer channel to periodically flip between output-compare and
         input-capture — **since ported, see the dedicated section below**). Implements
         `pwmDshotSetDirectionOutput()` (3-arg, non-telemetry signature at this initial-pass
         point in time), `pwmCompleteDshotMotorUpdate()`,
         `motor_DMA_IRQHandler()`, `pwmDshotMotorHardwareConfig()` using
         `tmr_base_init()`/`tmr_clock_source_div_set(TMR_CLOCK_DIV1)`/`tmr_cnt_dir_set(TMR_COUNT_UP)`
         directly (not `configTimeBase()`, to preserve the original's exact
         `lrintf(timerClock/hz + 0.01f) - 1` prescaler rounding), `tmr_output_default_para_init()`/
         `dma_default_para_init()` for struct defaults (matching the established AT-BSP
         convention from `timer_at32bsp.c`/`adc_at32f43x.c` rather than manual `memset`), and
         the already-portable `xDMA_*` macros / `DMA_IT_TCIF` flag helpers (already aliased in
         `drivers/dma.h`, no changes needed there).
         **Real latent bug found and avoided**: in the original `pwm_output_dshot.c`,
         `motor->timer->outputPeriod` is only ever assigned inside the `#ifdef
         USE_DSHOT_TELEMETRY` block of `pwmDshotMotorHardwareConfig()`, yet
         `pwmCompleteDshotMotorUpdate()` unconditionally reloads the timer's period register
         from it on every motor update — dead/unexercised upstream only because
         `USE_DSHOT_TELEMETRY` happens to always be enabled for every currently-real target
         (F4/F7/H7/G4). At the time this initial pass was written, AT32 did NOT yet enable
         `USE_DSHOT_TELEMETRY` either, so the new file set `outputPeriod` **unconditionally**
         (regardless of the `USE_DSHOT_TELEMETRY` define) to avoid inheriting this gap — this
         unconditional assignment was kept as-is (harmlessly redundant, not removed) once
         `USE_DSHOT_TELEMETRY` was later ported and enabled for AT32F43x (see the dedicated
         section below).
         **Also found (at this initial-pass point in time) and avoided**: AT-BSP's
         `tmr_output_channel_config()` (called via `timerOCInit()`) applies its
         `oc_output_state`/`occ_output_state` struct fields as part of the same call (their very
         names mean "output channel enable"/"output channel complementary enable") — unlike
         STM32 StdPeriph, which needs a **separate** `TIM_CCxCmd()`/`TIM_CCxNCmd()` call after
         `TIM_OCInit()` to actually enable the CCER output. An initial draft mirrored StdPeriph's
         separate-enable-call pattern using a local `AT_CH_SELECT()` macro, but this was removed
         as both unnecessary (redundant with the OC-config-struct fields) and subtly wrong (a
         `channel+1`-based complementary-channel selector calculation truncated incorrectly
         through the `>>1` conversion). Left the config-struct fields as the sole enable
         mechanism at that point in time, consistent with `pwm_output.c`'s AT32 branch.
         **Superseded once `USE_DSHOT_TELEMETRY` was ported** (see dedicated section below): an
         explicit `tmr_channel_enable()` disable/re-enable pair around `timerOCInit()` in
         `pwmDshotSetDirectionOutput()` turned out to be required after all — not for the reason
         above (the OC-config-struct fields still do apply the enable bits), but because
         switching the channel *back* from input-capture mode (bidirectional DSHOT telemetry)
         needs the channel explicitly disabled before reconfiguring it, and betaflight's own
         real AT32 port does the same disable/re-enable around every direction switch.
         **`dshot_dpwm.h` fix**: `DSHOT_DMA_BUFFER_UNIT`'s `#if defined(STM32F4) || ...`
         condition didn't include `AT32F43x`, so it was defaulting to `uint8_t` (wrong — DMA-to-
         CCR needs 32-bit words); added `|| defined(AT32F43x)`.
         **`common_pre.h`**: added `#define USE_DSHOT` to the AT32F43x block, with the
         DMAR/telemetry/bitbang deferrals documented inline. Note `USE_DSHOT_DMAR` is enabled by
         a separate, MCU-family-agnostic rule keyed only on `TARGET_FLASH_SIZE > 128` — AT32F435
         will very likely trip that rule once flash size is known for the unified target.
         **Resolved in the `USE_DSHOT_TELEMETRY` follow-up session** (see dedicated section
         below): an explicit `#ifdef AT32F43x #undef USE_DSHOT_DMAR #endif` was added right
         after that generic rule's closing `#endif`, so this can no longer be silently
         re-triggered once `TARGET_FLASH_SIZE` is defined for `AT32_UNIFIED`.
         **`make/mcu/AT32F4.mk`**: removed two stale/nonexistent-file references that would have
         broken any build attempt (`drivers/pwm_output_at32bsp.c` — no longer needed since
         `pwm_output.c` got an additive branch instead; `drivers/dshot_bitbang_at32bsp.c` — not
         written, `USE_DSHOT_BITBANG` deferred, `dshot_bitbang.c`/`dshot_bitbang_decode.c` stay
         listed as harmless `#ifdef`-guarded empty translation units), and replaced
         `drivers/pwm_output_dshot.c` with `drivers/pwm_output_dshot_at32bsp.c`.
         `drivers/pwm_output_dshot_shared.c` (the MCU-agnostic DSHOT packet-encode/DMA-rearm
         layer) and `drivers/dshot_dpwm.c` (MCU-agnostic device-init entrypoint) needed no
         changes — both already fully portable via the `xDMA_*` macros and generic types.
         **Not yet build-tested** — `get_errors` reports clean but is known-unreliable for AT32
         branches in this workspace (no `c_cpp_properties.json`); relies on manual `grep_search`
         verification against the vendored AT-BSP headers as usual.
- [x] WS2811 LED strip (`light_ws2811strip_at32f43x`) — **reconciled this session, was already
      written and build-verified**: this checklist item was stale — the file was already fully
      written (native AT-BSP replacement, same pattern as `pwm_output_dshot.c`/dshot), already
      listed in `AT32F4.mk`'s `MCU_COMMON_SRC`, and already compiling cleanly as part of every
      `make TARGET=AT32F435` build this session (`USE_LED_STRIP` is not undef'd anywhere for
      AT32, so it's an active, exercised code path, not dead code). Confirmed by reading the file
      directly — it's a real implementation, not a stub. See the "Status" note further down in
      this file, which already had this correct; this checklist entry had simply not been
      updated to match.
- [ ] USB VCP + MSC (`serial_usb_vcp_at32f4`, `usb_msc_at32f43x`)

Note: because this is a unified build (not a fixed per-board target), the driver layer
must support full runtime resource reconfiguration — dynamic pin-to-peripheral/timer/DMA
resolution driven by CLI `resource`/`timer`/`dma` commands — mirroring how
`SPI_FULL_RECONFIGURABILITY`/`I2C_FULL_RECONFIGURABILITY` work today for STM32F4, rather
than a fixed compiled-in `timerHardware[]`/DMA table for one board.

## 4. Target resource tables

- [ ] Full AT32F435 timer/alternate-function map (all pins/timers, not just one board's),
      to back the dynamic `timer`/`resource` CLI commands.
- [ ] Full AT32F435 DMA request map (all peripherals/streams), to back the dynamic `dma`
      CLI command.
- [ ] Validate the reference board's resource set (SPI1 gyro, SPI3 flash, TIM1/TIM3/TIM8
      motor/servo outputs, UART1/2/5, I2C1 — see Hardware notes above) resolves correctly
      once loaded via `.config` on the unified AT32F435 build.

## 5. Tooling / flashing

- [ ] Confirm flashing workflow: AT32 uses Artery's own ISP bootloader, not ST DFU.
      Verify the flashing tool used by Wingflight users supports it.

## 6. Bring-up validation

- [ ] Hardware bring-up: GPIO, UART, SPI (gyro), I2C, ADC (battery/current), timers,
      PWM/DSHOT outputs, USB VCP, blackbox/flash storage if present.
- [ ] No unit tests planned for this work (per project preference).

## Status

In progress. Vendor SDK is in-tree, build-system scaffolding (`AT32F4.mk`, `targets.mk`
wiring, `AT32_UNIFIED` target folder) exists, and the clock/RCC/persistent-storage/cycle-counter/
GPIO-IO/EXTI/DMA/SPI-bus/I2C-bus layer is written (compiles-by-inspection, cross-checked
function/enum names against the vendored AT-BSP headers and the `i2c_application_library`
middleware, but NOT yet build-tested — no `make TARGET=AT32F435` attempt has been made yet
because UART/timer/ADC driver files still don't exist, and the AT32 startup `.s` file + linker
script haven't been created). SPI is functionally complete EXCEPT the `bus_spi_pinconfig.c` AT32
pin/AF table, which is blocked pending datasheet-sourced pin mapping data from the user (see SPI
section above). I2C bus is functionally complete with real pin/AF data (sourced from betaflight's
own working AT32 port) — no blocker here. UART/serial is functionally complete too (see UART
section above), including a fix for a real gap where `USE_DMA_SPEC` was never defined for AT32 at
all (which would have silently disabled all of `dma_reqmap.c`'s AT32 content, including the I2C/SPI-
era DMA request-ID work). Timers are now functionally complete too (see Timers section above),
including a fix for another real gap: `USE_TIMER_MGMT` was never defined for AT32, which — per
`common_post.h`'s undef logic — would have silently disabled `USE_UNIFIED_TARGET` for every AT32
build (this project's unified-target architecture depends on it). ADC is now functionally
complete too (see ADC section above), including fixes for two more real gaps in the same
family as the earlier `USE_DMA_SPEC`/`USE_TIMER_MGMT` bugs: missing `DMA_REQUEST_ADC1/2/3` and
`DMA_REQUEST_TMRn_OVERFLOW` resolution aliases in `dma_reqmap.c` (both would have been
undefined-identifier compile errors, not silent gaps, so they'd have been caught at the first
build attempt regardless — found earlier by manual symbol verification instead). **Important
methodology note discovered this session**: `get_errors` (the IDE's IntelliSense-based
diagnostics) reported zero errors for `dma_reqmap.c` even with the missing macros still absent —
this workspace has no `c_cpp_properties.json`, so IntelliSense is not reliably evaluating the
`AT32F43x` `#if`/`#elif` branches against real AT32 defines. **Do not treat a clean `get_errors`
result as proof that AT32-specific code compiles** — manual `grep_search` verification of every
referenced macro/symbol against the vendored headers (as has been done throughout this port)
remains the only reliable check until a real `make TARGET=AT32F435` build is attempted. PWM
output and DSHOT (non-burst, non-telemetry) are now functionally complete too (see PWM/DSHOT
section above) — this phase also refined the port's own methodology: a full parallel
`_at32bsp.c` replacement file is only actually needed when a shared driver file has deep,
pervasive StdPeriph/register coupling (confirmed true for `pwm_output_dshot.c`, matching
`timer.c`'s precedent); small/localized StdPeriph usage in an otherwise-portable file
(`pwm_output.c`) is better handled with a small additive `#elif defined(AT32F43x)` branch —
inspect the actual function bodies before assuming which case applies. WS2811
(`light_ws2811strip_at32f43x.c`) is also now written, using the same native-replacement pattern
as DSHOT.

**First real `make TARGET=AT32F435` build attempts made this session** — this is now the
PRIMARY validation method going forward (far more reliable than `get_errors`, which has
repeatedly missed real bugs in AT32 branches — no `c_cpp_properties.json` in this workspace for
IntelliSense to evaluate AT32 defines against). Found and fixed, in order: (1) missing
`src/main/startup/startup_at32f435_437.s` (created, transcribed from the vendor GCC startup
file, simple Reset_Handler since `systemInit()` already calls `persistentObjectInit()`/
`checkForBootLoaderRequest()` in C); (2) missing `src/link/at32_flash_f43xg.ld` (created, modeled
on `stm32_flash_f411.ld`'s no-CCM structure, 1024K flash/192K RAM, reuses the existing generic
`stm32_flash_f4_split.ld` common file); (3) missing `$(CMSIS_DIR)/cm4/device_support` in
`INCLUDE_DIRS` (added — `at32f435_437.h` lives there); (4) wrong device-macro strategy in
`DEVICE_FLAGS` (needed a SPECIFIC vendor part macro like `AT32F435RGT7`, not a manually
pre-defined generic `AT32F435xx`, to satisfy the vendor header's own `#error`/self-definition
logic) plus a `__FPU_PRESENT` redefinition conflict (`1` vs vendor's `1U` — removed our
redundant pre-define); (5) `platform.h`'s AT32 dispatch branch was checking
`defined(AT32F435xx)` instead of our own `defined(AT32F43x)` — a chicken-and-egg bug since
`AT32F435xx` is only defined by the vendor header itself, which hadn't been included yet; (6) a
real macro-misuse bug in `dma_at32f43x.c` (`RCC_AHB1()` token-pastes its argument, so it can't
take a runtime ternary expression — fixed to apply the macro to each literal branch); (7)
`system_at32f43x.c` was missing `#include "drivers/nvic.h"` (needed for
`NVIC_PRIORITY_GROUPING`); (8) `timer_at32f43x.c` was left truncated by an earlier session — its
outer `#ifdef USE_TIMER` was never closed with a matching `#endif` (fixed). After all of the
above, the build gets past startup/linker/CMSIS/DMA/system-init/timers and compiles dozens of
MCU-generic driver files successfully.

**Major discovery this session**: `src/main/target/AT32_UNIFIED/target.mk` unconditionally sets
`FEATURES += VCP ONBOARDFLASH` for any target matching `AT32F435` — meaning a bare
`make TARGET=AT32F435` ALWAYS requires the USB VCP + MSC source files to exist (contradicting an
earlier session's now-corrected assumption that USB was deferrable/non-blocking). This is also
where `F435_TARGETS` actually gets populated (not in the central `make/targets_list.mk`, which
has no such list at all — it's contributed dynamically by each unified target's own
`target.mk`/`<TARGET>.mk` files). USB VCP + MSC (`vcp_at32/usbd_desc.c`, `vcp_at32/usbd_conf.c`,
`vcp_at32/usbd_cdc_interface.c`, `drivers/serial_usb_vcp_at32f4.c`, `drivers/usb_io.c`,
`drivers/usb_msc_at32f43x.c`, `msc/usbd_msc_desc.c`, `msc/usbd_storage.c`) is now the PRIMARY
remaining blocker for a bare build — reference implementations exist in
`betaflight/src/platform/AT32/` (a different USB device stack from STM32's VCP — Artery's own
`usbd_core` middleware) and should be ported next, watching for the same class of `io.h`/pin-AF-
table/`MCU_COMMON_SRC`/`common_pre.h` feature-flag gaps found in every prior peripheral port.

---

## MILESTONE (this session): `make TARGET=AT32F435 -j8` builds AND LINKS successfully

First-ever clean build: `obj/wingflight_4.6.0_AT32F435.hex` produced with zero compile/link
errors. Verified via actual `make` output plus a `Test-Path`/timestamp check on the generated
`.hex` (not just "no errors printed"). Full technical detail of every fix is in repo memory
(`/memories/repo/at32f435_port.md`, "MILESTONE" section) — summary:

- USB VCP + MSC port completed (full self-contained AT32 driver files + `AT32F4.mk` rewiring +
  `common_pre.h` flags + one vendor SDK bugfix in `usbd_sdr.c`).
- `config_streamer.c` AT32 flash-persistence port completed (5 additive branches using
  AT-BSP's `flash_unlock/flash_lock/flash_sector_erase/flash_word_program/flash_flag_clear`).
- `bus_spi.c`/`bus_spi_pinconfig.c` fixed: `system_core_clock` type mismatch, missing
  `#if !defined(AT32F43x)` guard around the `.stream` DMA descriptor field, and the 4
  `sckAF`/`misoAF`/`mosiAF`/`.af` guards in `spiPinConfigure()` were missing `AT32F43x` (simple
  oversight — `bus_spi_impl.h`'s struct fields were already correctly gated). The empty
  `spiHardware[]` array (genuinely no real AT32 pin/AF datasheet data available, and none to
  copy from betaflight's newer/incompatible AT32 platform tree) was resolved WITHOUT
  fabricating hardware data: a single inert `{ 0 }` sentinel entry keeps the array valid C,
  and is skipped by the pre-existing `if (!hw->reg) continue;` logic — **AT32 SPI pin
  assignment via the CLI `resource` system remains a real, intentional gap until real
  datasheet pin/AF data is supplied.**
- `drivers/system.c`: same `system_core_clock` type-mismatch bug as `bus_spi.c`.
- Disabled (AT32-only, real STM32-StdPeriph-timer-register code with no AT-BSP port, NOT
  fixed, tracked as follow-up work): `USE_FREQ_SENSOR`, `USE_PWM`, `USE_PPM`,
  `USE_SOFTSERIAL1`/`2`, `USE_ESCSERIAL`. This is a genuine, user-visible feature reduction for
  AT32 builds today (no PPM/PWM RX input, no softserial, no ESC 4-way passthrough, no
  freq/RPM sensor input) — each needs its own `_at32bsp.c`-equivalent port later.
- `io/serial_4way.c`: `Bit_RESET` (StdPeriph-only enum) shimmed to `false` for AT32.
- `sensors/adcinternal.c`: added a `getCoreTemperatureCelsius()` fallback stub (returns 0) for
  any target lacking `USE_ADC_INTERNAL` (this fork's `blackbox.c` calls it unconditionally,
  unlike upstream betaflight) — AT32's real internal-ADC temp/vrefint support is still
  unimplemented, just no longer a link failure.
- `make/mcu/AT32F4.mk`: added missing `drivers/bus_i2c_timing.c` (generic helper, link-time-only
  gap since it's not MCU-specific).
- `AT32_UNIFIED/target.mk`: added `SDCARD_SPI` to `FEATURES` (macro `USE_SDCARD_SPI` was already
  defined in `target.h` but the make-level FEATURES gate controlling which `.c` files get built
  was missing it — verified `sdcard*.c`/`asyncfatfs/*.c` have no non-portable STM32 code before
  enabling).
- `drivers/system_at32f43x.c`: added a trivial `void _init(void) {}` stub — required because the
  build uses `-nostartfiles` (so newlib-nano's own `crti.o`/`crtn.o` never get linked) yet
  `startup_at32f435_437.s` still calls `__libc_init_array()`, which unconditionally calls
  `_init()`. Matches betaflight's own AT32 platform glue precedent exactly.

**Known deliberate gaps after this milestone (tracked follow-up work, not bugs):**
`bus_spi_pinconfig.c` AT32 pin/AF table — **now ported, see below**,
`USE_ADC_INTERNAL` — **now ported, see below**, `USE_SOFTSERIAL1`/`2` — **now ported, see below**,
`USE_ESCSERIAL` — **now ported, see below**, `USE_FREQ_SENSOR` — **now ported, see below**,
`USE_DSHOT_TELEMETRY`/`USE_DSHOT_TELEMETRY_STATS` — **now ported, see below**,
`USE_DSHOT_DMAR` — **deliberately not ported, see below** (upstream betaflight's own AT32
implementation is itself broken/untested), `USE_DSHOT_BITBANG` (still pending, not yet
started). The build has NOT been flashed/tested on real hardware yet — this is "compiles and
links cleanly", not "verified working on a board".

## USE_PWM / USE_PPM (RX PWM/PPM input) — ported, build-verified

`drivers/rx/rx_pwm.c` needed only small additive `#elif defined(AT32F43x)` branches (its only two
StdPeriph/HAL-specific spots), not a full `_at32bsp.c` replacement — same "inspect the actual
function bodies before assuming full-file-port is needed" lesson already established for
`pwm_output.c` vs `pwm_output_dshot.c`:
- `pwmICConfig()` gained an AT32 branch using `tmr_input_config_type` + `tmr_input_channel_init()`
  (channel byte-offset → `tmr_channel_select_type` via `channel >> 1`; polarity passed as
  `TMR_INPUT_RISING_EDGE`/`TMR_INPUT_FALLING_EDGE` from all 4 call sites).
- `ppmAvoidPWMTimerClash()`'s raw `pwmTimer->PSC` read got an AT32 branch using `pwmTimer->div`
  (AT-BSP's prescaler register name, confirmed via vendored `at32f435_437_tmr.h`).
- Removed `USE_PWM`/`USE_PPM` from `common_post.h`'s AT32-only `#undef` block now that they're
  ported (`USE_SOFTSERIAL1/2`/`USE_ESCSERIAL` remain disabled there — unrelated files, not ported).
- Rebuilt `make TARGET=AT32F435 -j8` — clean compile+link, `EXITCODE=0`, fresh `.hex` produced
  (FLASH1 44.15% used, RAM 50.73% used). PWM/PPM RX input is now real, build-verified AT32
  functionality — still not flashed/tested on real hardware.
- **Follow-up idea recorded, not yet done**: `drivers/freq.c`'s `freqICConfig(timerHardware_t*,
  bool rising, uint16_t filter)` has the same signature/shape as `timer_at32bsp.c`'s already-working
  `timerChConfigIC()`/`timerChICPolarity()` — likely a similarly small fix to re-enable
  `USE_FREQ_SENSOR`, worth trying next.

## USE_FREQ_SENSOR (RPM/tachometer input capture) — ported, build-verified

Confirmed the predicted quick win above: `freqICConfig()` reuses `timer_at32bsp.c`'s existing
`timerChConfigIC()` directly (identical signature) as a 3rd `#elif defined(AT32F43x)` branch.
Additional scope found only after reading the full file:
- `FREQ_PRESCALER_MAX`: added `AT32F43x` to the `STM32F4||STM32G4||STM32F7` group (=0x0100) — a
  tuning constant (frequency headroom), not correctness-critical.
- `freqSetBaseClock()`: AT32 branch using `tim->div`/`tim->pr` (AT-BSP's PSC/ARR-equivalent
  register names) and `tim->swevt_bit.ovfswtr = 1` (equivalent to STM32's `TIM_EGR_UG`).
- Added a `FREQ_TIM_CNT(tim)` macro (`->cval` for AT32, `->CNT` otherwise) to portably replace the
  raw counter reads in `freqUpdate()`.
- **TMR2/TMR5 32-bit question RESOLVED** (previously a blocking unknown): found
  `tmr_32_bit_function_enable(tmr_type *tmr_x, confirm_state new_state)` in the vendored
  `at32f435_437_tmr.c`, whose own doc comment says "enable or disable tmr 32 bit function(plus
  mode)... only for TMR2/TMR5" (sets `ctrl1_bit.pmen`). This confirms AT32's TMR2/TMR5 DO support
  a 32-bit counter mode matching STM32's TIM2/TIM5 — but unlike STM32, it is NOT enabled by
  default and must be explicitly turned on. Implemented: `input->timer32 = (timer->tim == TMR2 ||
  timer->tim == TMR5); if (input->timer32) tmr_32_bit_function_enable(timer->tim, TRUE);` in
  `freqInit()`.
- Removed `#undef USE_FREQ_SENSOR` from `AT32_UNIFIED/target.h`.
- Rebuilt `make TARGET=AT32F435 -j8` after the TMR2/TMR5 fix — clean compile+link, `EXITCODE=0`.
  (Earlier interim rebuild, before this fix, also had `EXITCODE=0` with a byte-identical linked
  binary to the pre-freq.c build — expected/benign for an LTO build with no board `.config`
  loaded, since nothing references the freq-sensor symbols and whole-program dead-code elimination
  strips them either way; confirmed `freq.o` was genuinely recompiled via its timestamp.)

## USE_SOFTSERIAL1 / USE_SOFTSERIAL2 (software UART on any timer-capable pin) — ported, build-verified

`drivers/serial_softserial.c` needed 4 small additive `#elif defined(AT32F43x)` branches, no new
file:
- `serialEnableCC()` / the CC-disable call in `serialInputPortDeActivate()`: added AT32 branches
  using `tmr_channel_enable(tim, (tmr_channel_select_type)(channel >> 1), TRUE/FALSE)` (same
  channel-conversion trick used throughout this port) in place of `TIM_CCxCmd()`.
- `onSerialRxPinChange()`'s raw `tim->ARR / 2` counter-reset write: added an AT32 branch using
  `tim->cval = tim->pr / 2` (AT-BSP's CNT/ARR-equivalent register names).
- Added `AT32F43x` to the existing `#if defined(STM32F7) || defined(STM32H7) || defined(STM32G4)`
  quirk list (2 call sites) that explicitly re-enables the CC channel after every input-capture
  polarity change. Verified this is safe/correct for AT32 (not a guess): AT-BSP's own
  `tmr_input_channel_init()` (called by `timerChConfigIC()`) *already* unconditionally disables
  then re-enables the channel internally (`cXen = FALSE; ...; cXen = TRUE;` at the end of every
  channel's case, per `at32f435_437_tmr.c`) — so the extra explicit `serialEnableCC()` call is
  redundant-but-harmless (idempotent register write), matching the existing STM32F7/H7/G4
  precedent's effect exactly, just via a different underlying mechanism.
- Removed `USE_SOFTSERIAL1`/`USE_SOFTSERIAL2` from `common_post.h`'s AT32-only `#undef` block.
- Rebuilt `make TARGET=AT32F435 -j8` — clean compile+link, `EXITCODE=0`.

## USE_ESCSERIAL (ESC 4-way passthrough / bidirectional ESC serial) — ported, build-verified

`drivers/serial_escserial.c` needed 3 small additive `#elif defined(AT32F43x)` branches, no new
file:
- `TIM_DeInit()`: this file already had a local no-op stub for `USE_HAL_DRIVER` (HAL has no
  `TIM_DeInit` equivalent) with StdPeriph's own real `TIM_DeInit()` used directly otherwise. Added
  a 3rd definition for AT32 that calls AT-BSP's `tmr_reset(tim)` (confirmed equivalent — same
  per-instance `crm_periph_reset()` sequence used by `timer_at32bsp.c` elsewhere).
- 2 raw `TIM_SetCounter(tim, ...)` calls (one resetting to `ARR / 2`, one to `0`): added AT32
  branches writing `tim->cval` directly (`= tim->pr / 2` and `= 0` respectively).
- Removed `USE_ESCSERIAL` from `common_post.h`'s AT32-only `#undef` block (now empty — every
  timer-input-capture-based driver flagged in that block at the original build milestone has now
  been ported).
- Rebuilt `make TARGET=AT32F435 -j8` — clean compile+link, `EXITCODE=0`. All three of this
  session's follow-up ports (`USE_PWM`/`USE_PPM`, `USE_FREQ_SENSOR`, `USE_SOFTSERIAL1/2`,
  `USE_ESCSERIAL`) now build together cleanly in one final combined rebuild.

**Remaining known deliberate gaps** (not yet ported):
`USE_DSHOT_BITBANG` (not yet started). `USE_DSHOT_TELEMETRY`/`USE_DSHOT_TELEMETRY_STATS` are
now ported too, see dedicated section below. `USE_DSHOT_DMAR` is deliberately NOT ported (see
dedicated section below for reasoning). WS2811 LED
strip (`light_ws2811strip_at32f43x.c`) was reconciled this session — it was already written and is
already compiling/linking as an active code path (not a gap; the earlier checklist entry calling
it "missing"/build-blocking was stale and has been corrected). `USE_ADC_INTERNAL` (core
temp/vrefint) is now ported too, see dedicated section below. `bus_spi_pinconfig.c`'s AT32 pin/AF
table is now ported too (reused verbatim from betaflight's own vendored AT32 driver), see
dedicated section below. The build has still NOT been
flashed/tested on real hardware.

## USE_ADC_INTERNAL (core temp/vrefint sensor) — ported, build-verified

`drivers/adc_at32f43x.c` implements vrefint/tempsensor as an ADC1 preempt (injected) channel
pair, mirroring `drivers/adc_stm32f4xx.c`'s `ADC_InjectedChannelConfig`-based architecture 1:1
against AT-BSP's preempt-channel API — this repo's older single-selected-ADC-device model
(config->device picks ONE of ADC1/2/3), not betaflight's own newer per-channel-device AT32 port
(which uses a different, incompatible `adc.h`/`adc_impl.h` shape not present in this codebase).

- AT-BSP's "preempt channel" is the direct equivalent of STM32's "injected channel": confirmed
  via reading `at32f435_437_adc.h`/`.c` — `adc_preempt_channel_length_set()`/
  `adc_preempt_channel_set()` (1-based sequence 1-4, same as STM32's injected rank),
  `adc_preempt_conversion_trigger_set(..., ADC_PREEMPT_TRIG_EDGE_NONE)` (disables hardware
  triggering so only software-triggered conversions run, edge=NONE makes the trigger-source
  argument irrelevant — same semantics as STM32's `ADC_ExternalTrigInjecConvEdge_None`),
  `adc_preempt_software_trigger_enable()` (= `ADC_SoftwareStartInjectedConv`),
  `ADC_PCCE_FLAG` ("preempt channel conversion end" = STM32's `ADC_FLAG_JEOC`), and
  `adc_preempt_conversion_data_get(ADC1, ADC_PREEMPT_CHANNEL_1/2)` (= `ADC_GetInjectedConversionValue`).
- `adc_common_config_type.tempervintrv_state` (confirmed via reading the AT-BSP struct/impl
  directly, default `FALSE`, maps to `ADCCOM->cctrl_bit.itsrven`) is AT32's direct equivalent of
  STM32's `ADC_TempSensorVrefintCmd(ENABLE)` — set `TRUE` only when `USE_ADC_INTERNAL` is defined.
- Channel numbers: `ADC_CHANNEL_17` = vrefint, `ADC_CHANNEL_16` = tempsensor on ADC1 — **not
  guessed**, taken directly from betaflight's own official (already-vendored-in-this-repo) AT32
  ADC driver at `betaflight/src/platform/AT32/adc_at32f43x.c`
  (`#define ADC_CHANNEL_VREFINT ADC_CHANNEL_17` / `#define ADC_CHANNEL_TEMPSENSOR_ADC1
  ADC_CHANNEL_16`).
- **Calibration constants — real datasheet data, not fabricated**: AT32F435/437 has no
  factory-programmed OTP calibration words for vrefint/tempsensor, unlike STM32 (confirmed by
  the complete absence of any `CAL_ADDR`-style macro for AT32 anywhere in betaflight's own AT32
  platform headers — betaflight itself uses fixed nominal per-datasheet constants instead of
  reading OTP). Reused betaflight's exact values verbatim from
  `betaflight/src/platform/common/stm32/platform/adc_impl.h`'s `#ifdef AT32F435` block:
  `VREFINT_EXPECTED = 1489`, `VREFINT_CAL_VREF = 3300`, `TEMPSENSOR_CAL_VREFANALOG = 3300`,
  `TEMPSENSOR_CAL1_TEMP = 25`, `TEMPSENSOR_CAL1_V = 1.27f`, `TEMPSENSOR_SLOPE = -4.13f` (mV/C).
  Added these as a new `#ifdef AT32F43x` block in this repo's own `drivers/adc_impl.h` (which
  already had equivalent per-MCU blocks for STM32F4/F7), alongside the existing
  `VREFINT_CAL_VREF`/`TEMPSENSOR_CAL_VREFANALOG`/`TEMPSENSOR_CAL1_TEMP` macros shared with
  `drivers/adc.c`'s MCU-agnostic `adcInternalCompensateVref()`/`adcInternalComputeTemperature()`
  (unchanged, already fully portable).
- New `adcInitInternalInjected(config)` (static, mirrors the STM32F4 function of the same name)
  sets up the ADC1 preempt sequence and computes `adcVREFINTCAL`/`adcTSCAL1`/`adcTSSlopeK` from
  the constants above using betaflight's exact formula (`adcTSCAL1 = lrintf((TEMPSENSOR_CAL1_V *
  4095.0f) / 3.3f)`, `adcTSSlopeK = lrintf(-3300.0f * 1000.0f / 4095.0f / TEMPSENSOR_SLOPE)`) —
  `adcTSCAL2` intentionally left unused/zero, matching betaflight's AT32 port exactly (the shared
  `adcInternalComputeTemperature()` formula never reads `adcTSCAL2`, only other MCUs' slope-from-two-
  calibration-points derivation does).
- `adcInternalIsBusy()`/`adcInternalStartConversion()` reuse the exact same
  `static bool adcInternalConversionInProgress` stateful pattern as `adc_stm32f4xx.c` (checked via
  reading that file's implementation) — **deliberately not simplified to a stateless
  `adc_flag_get(...) == RESET` check**: traced through `adcinternal.c`'s call sequence and found
  that a stateless check would deadlock (`adcInternalIsBusy()` must return `false` before any
  conversion has ever been started, but the PCCE flag is also `RESET` in that same initial state,
  which a naive stateless implementation would misread as "still busy" forever).
- `adcInternalReadVrefint()`/`adcInternalReadTempsensor()` read `ADC_PREEMPT_CHANNEL_1`/`_2` via
  `adc_preempt_conversion_data_get()`.
- `adcInit()`: added a `device != ADCDEV_1` branch that gives ADC1 its own minimal
  clock/base-config/resolution/enable/calibration sequence (mirroring
  `adc_stm32f4xx.c`'s "if (device != ADCDEV_1 || !adcActive)" handling) so the preempt channels
  work correctly even if the config's main selected external-channel device is ADC2/ADC3 instead
  of ADC1. The pre-existing external-channel/DMA/ordinary-channel code path is otherwise
  completely unchanged and still only runs when `adcActive` is true — deliberately did **not**
  let a zero-channel `adc_base_config()`/DMA setup run for the selected device in the edge case of
  "internal-only, no external channels enabled, non-ADC1 device selected", since
  `ordinary_channel_length = 0` would underflow AT-BSP's internal `length - 1` field encoding;
  added an explicit `if (!adcActive) { return; }` right after the internal-channel setup (inside
  the `#ifdef USE_ADC_INTERNAL` block) to guarantee that path is never reached.
- Enabled `USE_ADC_INTERNAL` in `common_pre.h`'s `AT32F43x` block. Removed the stale
  "AT32F43x pending a real temp-sensor/vrefint injected-conversion implementation" comment in
  `sensors/adcinternal.c` (that `#else` branch is now dead code on AT32, since `USE_ADC_INTERNAL`
  is defined).
- Rebuilt `make TARGET=AT32F435 -j8` — clean compile+link, `EXITCODE=0`, zero warnings.

## bus_spi_pinconfig.c AT32 pin/AF table — ported, build-verified

Previously marked **BLOCKED**, believing no real AT32F435 SPI pin → alternate-function mapping
existed anywhere accessible. That assumption was wrong: betaflight's own vendored AT32 port,
already present in this repo at
[betaflight/src/platform/common/stm32/bus_spi_pinconfig.c](betaflight/src/platform/common/stm32/bus_spi_pinconfig.c),
contains a complete, real, datasheet-derived `#ifdef AT32F4` `spiHardware[]` block for SPI1-4.
Copied it verbatim into this repo's
[src/main/drivers/bus_spi_pinconfig.c](src/main/drivers/bus_spi_pinconfig.c) rather than
fabricating or guessing pin/AF data.

- Betaflight's newer SPI architecture (`spiPinDef_t` with a per-pin `.af` field) is structurally
  identical to what this repo already had wired up for `AT32F43x` (grouped with STM32F7/H7/G4's
  per-pin-AF code paths in `bus_spi_impl.h`/`bus_spi_pinconfig.c` from an earlier session), so no
  new architecture/plumbing was needed — this was a pure data port.
- Betaflight's AT32 table needs 5 pin-select slots (SPI3's MOSI list has 5 real entries: PB0,
  PB2, PB5, PC12, PD0), but this repo's `AT32F43x` branch of `MAX_SPI_PIN_SEL` was still set to
  4 (inherited from being grouped with STM32F7). Rather than silently truncating real pin data
  to fit, bumped `MAX_SPI_PIN_SEL` to 5 for `AT32F43x` in
  [src/main/drivers/bus_spi_impl.h](src/main/drivers/bus_spi_impl.h) — this exactly matches
  betaflight's own `src/platform/AT32/include/platform/platform.h` (`#define MAX_SPI_PIN_SEL 5`),
  confirming 5 is the correct real value for this MCU family, not an arbitrary choice.
- Verified `SPI1`/`SPI2`/`SPI3`/`SPI4` (`spi_type*` aliases) and `CRM_SPI1_PERIPH_CLOCK` through
  `CRM_SPI4_PERIPH_CLOCK` all exist in the vendored `at32f435_437_spi.h`/`at32f435_437_crm.h`
  before relying on them; `RCC_APB1(SPI2/3)`/`RCC_APB2(SPI1/4)` already resolve correctly for
  `AT32F43x` via this repo's pre-existing `rcc.h` macro redefinitions (`RCC_APB1(periph)` →
  `CRM_ ## periph ## _PERIPH_CLOCK`), so no `rcc.h` changes were needed.
- Removed the now-obsolete `SPI_HARDWARE_TABLE_NOT_YET_POPULATED` sentinel/all-zero-entry
  workaround that previously kept `spiHardware[]` non-empty on AT32.
- Rebuilt `make TARGET=AT32F435 -j8` — clean compile+link, `EXITCODE=0`, zero warnings. As
  always, SPI device/pin assignment via CLI `resource` commands on real AT32 hardware has not
  been flashed/tested — this only confirms the table compiles and the pin-matching logic in
  `spiPinConfigure()` runs over real (not placeholder) data.

## USE_DSHOT_TELEMETRY / USE_DSHOT_TELEMETRY_STATS — ported, build-verified

Bidirectional DSHOT (ESC telemetry returned on the same signal wire, direction-switched
per-frame between output-compare and input-capture) is now implemented for AT32F43x, ported
from betaflight's own real, working upstream AT32 driver
([betaflight/src/platform/AT32/pwm_output_dshot.c](betaflight/src/platform/AT32/pwm_output_dshot.c)),
cross-checked against this repo's own STM32 StdPeriph reference file
([src/main/drivers/pwm_output_dshot.c](src/main/drivers/pwm_output_dshot.c)) for exact
conventions/naming/struct-field usage. `USE_DSHOT_DMAR` remains deliberately NOT ported (see
its own section immediately below).

**Two platform-level prerequisites** (neither existed for AT32 before this work):
- `TIM_ICInitTypeDef` type alias — added
  `#define TIM_ICInitTypeDef tmr_input_config_type` to
  [src/main/common/platform.h](src/main/common/platform.h), right after the existing
  `TIM_OCInitTypeDef` alias. Needed because `drivers/dshot_dpwm.h`'s `motorDmaOutput_t` struct
  has an `icInitStruct` field of this type, used to reconfigure the timer channel for
  input-capture mode when switching direction to read telemetry. This matches betaflight's own
  upstream alias of the same name.
- `TIM_DMACmd()` function — the shared, MCU-agnostic
  [src/main/drivers/pwm_output_dshot_shared.c](src/main/drivers/pwm_output_dshot_shared.c) calls
  `TIM_DMACmd()` unconditionally under `USE_DSHOT_TELEMETRY` (STM32F4 gets this straight from its
  vendor StdPeriph library; AT-BSP has no equivalent). Added a prototype to
  [src/main/drivers/timer.h](src/main/drivers/timer.h) (guarded `#if defined(AT32F43x)`, inside
  the non-HAL branch) and a one-line StdPeriph-signature-compatible wrapper implementation in
  [src/main/drivers/timer_at32bsp.c](src/main/drivers/timer_at32bsp.c) that calls AT-BSP's
  `tmr_dma_request_enable()`, since the `source` bitfield values (`TIM_DMA_CCx`/`TIM_DMA_Update`)
  map 1:1 onto AT-BSP's `tmr_dma_request_type` bits.

**[src/main/drivers/pwm_output_dshot_at32bsp.c](src/main/drivers/pwm_output_dshot_at32bsp.c)
rewrite** (build-verified, `EXITCODE=0`, zero warnings):
- `static tmr_channel_select_type toCHSelectType(uint8_t channel, bool useNChannel)` (new) — maps
  a 1-based Betaflight channel number + N-channel flag to the AT-BSP
  `TMR_SELECT_CHANNEL_1/2/3/4` or `_1C/_2C/_3C` enum values needed by
  `tmr_primary_mode_select()`/`tmr_channel_enable()`.
- `#ifdef USE_DSHOT_TELEMETRY void dshotEnableChannels(uint8_t motorCount)` (new) — loops all
  DSHOT motors, calling `tmr_primary_mode_select()` + `tmr_channel_enable()` per motor, using
  `timerHardware->output & TIMER_OUTPUT_N_CHANNEL` (confirmed as the always-accurate source for
  this flag, not the unused `motor->output` field, since `dshot_dpwm.c`'s `dshotPwmDevInit()`
  passes `timerHardware->output` through).
- `pwmDshotSetDirectionOutput()` — signature is now conditional: 1-arg
  (`motorDmaOutput_t * const motor`) under `USE_DSHOT_TELEMETRY`, using the motor-embedded
  `ocInitStruct`/`dmaInitStruct` fields via `OCINIT`/`DMAINIT` macros; 3-arg (explicit
  `TIM_OCInitTypeDef*`/`DMA_InitTypeDef*` params) otherwise — matching this repo's own STM32
  reference file's exact pattern. Also applies an AT-BSP SDK workaround (ported verbatim from
  betaflight, confirmed necessary): `tmr_output_channel_config()` does **not** clear a channel's
  CxC capture/compare-selection bitfield in the `cm1_output_bit`/`cm2_output_bit` registers when
  switching back from input-capture mode, so `timer->cm1_output_bit.c1c`/`.c2c` (ch1/ch2) and
  `timer->cm2_output_bit.c3c`/`.c4c` (ch3/ch4) are manually zeroed before calling `timerOCInit()`.
- `#ifdef USE_DSHOT_TELEMETRY static void pwmDshotSetDirectionInput(motorDmaOutput_t * const
  motor)` (new) — sets `motor->isInput = true`, records `inputStampUs` via `micros()` on first
  entry, disables the output-compare period buffer, sets `timer->pr = 0xffffffff` (max period so
  the input-capture window doesn't wrap early), calls
  `tmr_input_channel_init(timer, &motor->icInitStruct, TMR_CHANNEL_INPUT_DIV_1)`, and
  reconfigures the DMA stream's direction to `DMA_DIR_PERIPHERAL_TO_MEMORY` for reading GCR
  telemetry edges.
- `motor_DMA_IRQHandler()` — under telemetry, now records
  `dshotDMAHandlerCycleCounters.irqAt = getCycleCounter()` near the top, and its tail
  unconditionally (when `useDshotTelemetry`) calls `pwmDshotSetDirectionInput()`, sets the DMA
  transfer count to `GCR_TELEMETRY_INPUT_LEN`, re-enables the DMA stream + DMA request, and
  records `dshotDMAHandlerCycleCounters.changeDirectionCompletedAt`.
- `pwmDshotMotorHardwareConfig()` — added the `OCINIT`/`DMAINIT` macro pair at the top of the
  function (resolving to the motor-embedded structs under telemetry, local stack structs
  otherwise), matching this repo's own STM32 reference file's convention exactly (no `#undef` at
  the end, since this is the last function in the file, again matching upstream). Added
  `output ^= TIMER_OUTPUT_INVERTED` under telemetry (confirmed safe: `TIMER_OUTPUT_INVERTED =
  (1<<0)` and `TIMER_OUTPUT_N_CHANNEL = (1<<1)` are distinct bits, per
  [src/main/drivers/timer.h](src/main/drivers/timer.h)). All direct `ocInitStruct.`/
  `dmaInitStruct.` field references were changed to go through the `OCINIT`/`DMAINIT` macros.
  Added the `icInitStruct` setup block (input-capture polarity/filter config) under telemetry,
  a `dshotTelemetryDeadtimeUs` computation, and a conditional call to
  `pwmDshotSetDirectionOutput()` (1-arg under telemetry, 3-arg otherwise). Added a final
  `*timerChCCR(timerHardware) = 0xffff` startup-safety line under telemetry (ensures the ESC
  sees a safe/idle duty cycle before the first real DSHOT frame is armed).
- A self-introduced bug (duplicated top-of-file scope comment left behind by an imprecise
  `replace_string_in_file` edit) was found via a follow-up full-file re-read and fixed before the
  final build verification.

**`common_pre.h`**: `USE_DSHOT_TELEMETRY`/`USE_DSHOT_TELEMETRY_STATS` added to the AT32F43x
`#define` block, with the comment block above it rewritten to document the current, accurate
per-flag status (TELEMETRY/STATS ported this session; DMAR deliberately not ported; BITBANG
still deferred/not started).

Final rebuild after all of the above: `make TARGET=AT32F435 -j8` → `EXITCODE=0`, zero warnings,
full link succeeded (FLASH1 455586 B / 992 KB = 44.85% used, RAM 103324 B / 192 KB = 52.55%
used).

## USE_DSHOT_DMAR — deliberately NOT ported

`USE_DSHOT_DMAR` (STM32-style 4-channel timer-update-event burst DMA via the timer's `->DMAR`
alias register, letting one DMA transfer update all 4 CCR registers plus ARR/etc. in one burst)
is intentionally **not** ported to AT32F43x, and this is a considered decision rather than an
oversight:

- Betaflight's own real, upstream AT32 DSHOT DMAR implementation is itself admittedly
  broken/untested — it contains a `// NB burst mode not tested` comment and a commented-out
  `TIM_DMA_Update`-equivalent call marked `XXX TODO`. Porting known-broken/untested code for a
  safety-critical motor-timing path would be irresponsible, so it was deliberately excluded.
- AT-BSP's timer peripheral has no register directly equivalent to STM32's `TIMx->DMAR`/`DCR`
  alias-address burst-DMA mechanism in the first place, so a real port (if ever undertaken) would
  need its own from-scratch design, not a mechanical translation — there is currently no working
  reference implementation (upstream or otherwise) to safely copy from.
- **Defensive guard added**: [src/main/target/common_pre.h](src/main/target/common_pre.h) has a
  separate, MCU-family-agnostic rule (`#if (TARGET_FLASH_SIZE > 128) #define USE_DSHOT_DMAR ...
  #endif`) that could silently re-enable `USE_DSHOT_DMAR` for AT32 once `TARGET_FLASH_SIZE` is
  eventually defined for the AT32 unified target (currently undefined for AT32 — only
  `SITL/target.h` defines it today, so this rule is dormant/harmless for now, but not
  permanently safe on its own). Added
  `#ifdef AT32F43x #undef USE_DSHOT_DMAR #endif` immediately after that generic rule's closing
  `#endif`, making the "AT32 never does DMAR" decision permanently enforced in the actual
  preprocessor logic regardless of future `TARGET_FLASH_SIZE` changes, not just documented in a
  comment.
- Revisit only if/when a real, working AT32 DMAR reference implementation becomes available
  (upstream betaflight or otherwise) — until then, this is a closed decision, not an open gap.

## USE_DSHOT_BITBANG — ported, build-verified

`USE_DSHOT_BITBANG` (software/DMA-driven "bitbang" DSHOT output on arbitrary GPIO pins, not
requiring a hardware timer-channel-to-pin mapping — used for high motor counts and for pins
without a usable timer resource) is now ported to AT32F43x.

Betaflight's own upstream tree has **two** related files for this feature, and only one of them
was actually portable:

- `betaflight/src/platform/AT32/dshot_bitbang.c` (the higher-level shared motor/port/pacer logic)
  turned out to be from a **newer, incompatible, refactored architecture** — confirmed via
  `bbFindMotorPacer(timerResource_t *tim)` (a `timerResource_t`-based abstraction) vs. this
  repo's own `bbFindMotorPacer(TIM_TypeDef *tim)`. This file was **not** used as a reference for
  edits; this repo's own existing, MCU-agnostic
  [src/main/drivers/dshot_bitbang.c](src/main/drivers/dshot_bitbang.c) was kept as the base and
  only given small additive `#elif defined(AT32F43x)` branches (below).
- `betaflight/src/platform/AT32/dshot_bitbang_stdperiph.c` (the low-level, per-MCU register/DMA
  file, despite its "stdperiph" name still being genuinely AT-BSP-native code) **was** directly
  portable, since it implements the same `bb*()` function contract that this repo's own STM32
  [src/main/drivers/dshot_bitbang_stdperiph.c](src/main/drivers/dshot_bitbang_stdperiph.c)/
  [src/main/drivers/dshot_bitbang_ll.c](src/main/drivers/dshot_bitbang_ll.c) files implement. This
  was ported (with careful symbol-by-symbol cross-verification against both betaflight's and
  this repo's own vendored `lib/main/AT32F43x` SDK headers) into a brand-new
  [src/main/drivers/dshot_bitbang_at32bsp.c](src/main/drivers/dshot_bitbang_at32bsp.c).

**New file: [src/main/drivers/dshot_bitbang_at32bsp.c](src/main/drivers/dshot_bitbang_at32bsp.c)**
implements the full low-level `bb*()` API declared in
[src/main/drivers/dshot_bitbang_impl.h](src/main/drivers/dshot_bitbang_impl.h):
`bbGpioSetup()`, `bbTimerChannelInit()`, `bbLoadDMARegs()`/`bbSaveDMARegs()` (under
`USE_DMA_REGISTER_CACHE`), `bbSwitchToOutput()`, `bbSwitchToInput()` (under
`USE_DSHOT_TELEMETRY`), `bbDMAPreconfigure()`, `bbTIM_TimeBaseInit()`, `bbTIM_DMACmd()`,
`bbDMA_ITConfig()`, `bbDMA_Cmd()`, `bbDMA_Count()`. Notable AT-BSP-specific details resolved
during porting (all confirmed via grep against the vendored SDK headers before writing code,
not just copied blindly from betaflight):

- `BB_CH_SELECT(ch)` — a local, file-scoped macro (`((tmr_channel_select_type)((ch) >> 1))`)
  converting this repo's byte-offset timer channel convention (0/4/8/12) to AT-BSP's
  `tmr_channel_select_type` enum values (0/2/4/6). Mirrors the identical, already-existing
  private `AT_CH_SELECT` macro in
  [src/main/drivers/timer_at32bsp.c](src/main/drivers/timer_at32bsp.c) — redefined locally
  rather than exported/shared, to avoid new header coupling for one line of arithmetic.
- GPIO direct register access uses AT-BSP's `gpio_type` field names: `cfgr` (mode register,
  STM32 `MODER` equivalent), `scr` (set/clear register, exact STM32 `BSRR` equivalent — bits
  0-15 set, bits 16-31 clear), `idt` (input data register).
- `TIM_OCStructInit()` (STdPeriph-only, no AT-BSP equivalent) replaced with AT-BSP's real
  `tmr_output_default_para_init()` — confirmed by checking how
  [src/main/drivers/pwm_output_dshot_at32bsp.c](src/main/drivers/pwm_output_dshot_at32bsp.c)
  (from the earlier `USE_DSHOT_TELEMETRY` port) already solves the exact same problem.
- `TIM_CtrlPWMOutputs()` (StdPeriph-only, called from the `DEBUG_MONITOR_PACER` debug block —
  which is **not** a dead branch, since `DEBUG_MONITOR_PACER` is unconditionally `#define`d near
  the top of `dshot_bitbang_impl.h`, so this code path is genuinely compiled) replaced with
  AT-BSP's `tmr_output_enable()`, confirmed as the correct 1:1 substitute by checking
  [src/main/drivers/pwm_output.c](src/main/drivers/pwm_output.c)'s own pre-existing
  `#elif defined(AT32F43x)` branch, which makes the identical substitution.
- `WRITE_REG`/`READ_REG`/`MODIFY_REG` (STM32 CMSIS-device-header macros, used for the direct
  `cfgr`/`scr` register read-modify-write in `bbSwitchToOutput()`/`bbSwitchToInput()`) have
  **no AT-BSP equivalent** (AT32's vendor SDK headers don't define them, unlike STM32's CMSIS
  device headers) — confirmed missing via grep across both this repo's own and betaflight's
  vendored `lib/main/AT32F43x` tree. Added the same three macros betaflight's own upstream AT32
  `platform.h` defines, to this repo's
  [src/main/common/platform.h](src/main/common/platform.h), inside the `#elif defined(AT32F43x)`
  block.
- `DMA_IT_TCIF` (this repo's own [src/main/drivers/dma.h](src/main/drivers/dma.h) constant, used
  for AT32) confirmed as the correct choice over `DMA_IT_TC` (STM32 vendor-header-only, used by
  the STM32 `dshot_bitbang_stdperiph.c` reference file).
- `dma_init_type` field names (`peripheral_inc_enable`, `memory_inc_enable`,
  `loop_mode_enable`, `direction`/`dma_dir_type`, `buffer_size`, `peripheral_base_addr`,
  `peripheral_data_width`, `memory_base_addr`, `memory_data_width`, `priority`/
  `dma_priority_level_type`) all confirmed present and correctly named via the vendored
  `at32f435_437_dma.h`.
- `tmr_base_init(tmr_type*, uint32_t period, uint32_t div)` takes scalar arguments directly (no
  StdPeriph-style init-struct); `bbPort_t.timeBaseInit` was given a plain, deliberately-unused
  `uint32_t` placeholder field type for AT32 (documented via comment) instead of a fabricated
  fake struct alias.

**Small additive `#elif defined(AT32F43x)` branches** (this repo's own files, MCU-agnostic
shared logic kept intact):
- [src/main/drivers/dshot_bitbang_impl.h](src/main/drivers/dshot_bitbang_impl.h) —
  `BB_GPIO_PULLDOWN`/`BB_GPIO_PULLUP` (→ `GPIO_PULL_DOWN`/`GPIO_PULL_UP`, AT32's own `io.h`
  naming, distinct from STM32's `GPIO_PuPd_DOWN`/`_UP`); `dmaRegCache_t` register-cache fields
  (`ctrl`/`dtcnt`/`paddr`/`maddr`, matching AT-BSP's `dma_channel_type` field names); the
  `timeBaseInit` placeholder field described above.
- [src/main/drivers/dshot_bitbang.c](src/main/drivers/dshot_bitbang.c) — buffer-attribute
  macros (`FAST_DATA_ZERO_INIT`, matching the existing STM32G4/F7 branch, no cache-coherency
  concerns on Cortex-M4); the `bbTimerHardware[]` pacer-timer table (8 entries: `TMR8`
  CH1-CH4 then `TMR1` CH1-CH4 via `DEF_TIM(..., TIM_USE_NONE, 0, chan_idx, 0)` — mirrors
  betaflight's own AT32 bitbang pacer table exactly); the motor `iocfg` computation in
  `dshotBitbangDevInit()` (`IO_CONFIG(GPIO_MODE_OUTPUT, GPIO_DRIVE_STRENGTH_STRONGER,
  GPIO_OUTPUT_PUSH_PULL, bbPuPdMode)`, matching AT32's own `io.h` macro naming).

**Build wiring**:
- [make/mcu/AT32F4.mk](make/mcu/AT32F4.mk): added `drivers/dshot_bitbang_at32bsp.c` to
  `MCU_COMMON_SRC`, next to the already-listed `drivers/dshot_bitbang.c`/
  `drivers/dshot_bitbang_decode.c`.
- `common_pre.h`: `USE_DSHOT_BITBANG` added to the AT32F43x `#define` block; the explanatory
  comment above it (previously describing BITBANG as unported/deferred) rewritten to describe
  the now-completed port and its two small platform.h prerequisites.

Final build after all of the above: `make TARGET=AT32F435 -j8` → exit code 0, zero compiler
errors, full link succeeded (`arm-none-eabi-size` on the resulting `.elf`: `text=454230
data=10048 bss=96112`, i.e. ~464 KB flash / ~104 KB RAM used). All three touched/new
`dshot_bitbang*.o` object files were confirmed freshly rebuilt (not stale) via file-timestamp
check before accepting the result.





