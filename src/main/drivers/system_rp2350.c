/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Betaflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <string.h>

#include "platform.h"

#include "common/maths.h"
#include "common/time.h"
#include "drivers/system.h"
#include "drivers/time.h"

#include "drivers/io.h"
#include "drivers/light_led.h"
#include "drivers/motor.h"
#include "drivers/persistent.h"
#include "drivers/sound_beeper.h"

#include "flight/servos.h"
#include "flight/motors.h"

#include "platform/multicore.h"

#include "hardware/clocks.h"
#include "hardware/regs/m33.h"
#include "hardware/structs/m33.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/runtime.h"
#include "pico/unique_id.h"
// flash.h used by PICO QSPI helpers is included where needed in PICO bus/flash code

///////////////////////////////////////////////////

// SystemInit and SystemCoreClock variables/functions,
// as per pico-sdk rp2_common/cmsis/stub/CMSIS/Device/RP2350/Source/system_RP2350.c

uint32_t SystemCoreClock; /* System Clock Frequency (Core Clock)*/

void SystemCoreClockUpdate (void)
{
    SystemCoreClock = clock_get_hz(clk_sys);
}

void __attribute__((constructor)) SystemInit (void)
{
    // pico-sdk's real runtime_init() lives in pico_clib_interface/*.c, which is
    // not part of this build (those files are compiled against the SDK's own
    // include set and collide with wingflight's headers - e.g. <time.h> resolves
    // to src/main/common/time.h). The link therefore silently binds runtime_init
    // to crt0.S's weak no-op stub, and NONE of the SDK's preinit entries run:
    // clocks are left unconfigured (clock_get_hz() reports the uninitialised
    // cached 0, so SystemCoreClock and usTicks below would both be 0) and CPACR
    // CP10/CP11 stay clear, so the first hardware FP instruction taken faults.
    // Observed on RP2350 hardware as a NOCP UsageFault escalated to HardFault at
    // the `vpush {d8}` entering validateAndFixGyroConfig().
    //
    // Run the initializers here instead. systemInit() is the first statement in
    // init(), so this is the earliest firmware-controlled point. This mirrors the
    // STM32 ports, which enable CP10/CP11 in their own SystemInit().
    //
    // Note the constructor attribute above is inert for the same reason (nothing
    // walks .init_array either); SystemInit() is called explicitly by systemInit().
    runtime_run_initializers();

    SystemCoreClockUpdate();
}

////////////////////////////////////////////////////

void systemResetHard(void)
{
    bprintf("*** PICO systemResetHard ***");
    //TODO: check

#if 1
#ifdef USE_MULTICORE
    // Reset core 1
    multicore_reset_core1();
#endif
    watchdog_reboot(0, 0, 0);
#else
    // this might be fine
    __disable_irq();
    NVIC_SystemReset();
#endif
}

// Generic systemReset(int reason) (drivers/system.c's version is excluded
// from the PICO build - see make/mcu/RP2350.mk MCU_EXCLUDES - because it
// pulls in drivers/timer.h, which has no PICO branch and isn't needed here).
void systemReset(int reason)
{
#ifdef USE_PERSISTENT_OBJECTS
    persistentObjectWrite(PERSISTENT_OBJECT_RESET_REASON, reason);
#else
    UNUSED(reason);
#endif

    motorStop();
    motorShutdown();
    servoShutdown();

    systemResetHard();
}

uint32_t systemUniqueId[3] = { 0 };

// cycles per microsecond
static uint32_t usTicks = 0;
static float usTicksInv = 0.0f;

// These are defined in pico-sdk headers as volatile uint32_t types
#define PICO_DWT_CTRL   m33_hw->dwt_ctrl
#define PICO_DWT_CYCCNT m33_hw->dwt_cyccnt
#define PICO_DEMCR      m33_hw->demcr

void cycleCounterInit(void)
{
    // TODO check clock_get_hz(clk_sys) is the clock for CPU cycles
    usTicks = SystemCoreClock / 1000000;
    usTicksInv = 1e6f / SystemCoreClock;

    // Global DWT enable
    PICO_DEMCR |= M33_DEMCR_TRCENA_BITS;

    // Reset and enable cycle counter
    PICO_DWT_CYCCNT = 0;
    PICO_DWT_CTRL |= M33_DWT_CTRL_CYCCNTENA_BITS;
}

// Enter the RP2350 ROM (BOOTSEL) bootloader if the previous boot requested it.
//
// cli.c's `bl rom`, MSP reboot and drivers/system.c all request the ROM
// bootloader the same way: persist RESET_BOOTLOADER_REQUEST_ROM as the reset
// reason, then reboot. Acting on that flag is the platform's job - every STM32
// family has its own checkForBootLoaderRequest() (see system_stm32f4xx.c) - and
// the PICO port had no equivalent, so the request was written and then silently
// ignored on the next boot: `bl rom` just restarted the firmware.
//
// The persistent store lives in .uninitialized_data.persistent (RAM below
// .text, so the PICO_COPY_TO_RAM startup copy does not clobber it) and survives
// the watchdog reboot systemResetHard() performs.
//
// rom_reset_usb_boot_extra() does not return.
static void checkForBootLoaderRequest(void)
{
    if (persistentObjectRead(PERSISTENT_OBJECT_RESET_REASON) != RESET_BOOTLOADER_REQUEST_ROM) {
        return;
    }

    // Clear first: if the flag survived into the bootloader and back, a failed
    // or cancelled flash would otherwise trap the board in a BOOTSEL loop.
    persistentObjectWrite(PERSISTENT_OBJECT_RESET_REASON, RESET_NONE);

    rom_reset_usb_boot_extra(-1, 0, false);
    while (1);
}

void systemInit(void)
{
    //TODO: implement

    SystemInit();

    checkForBootLoaderRequest();

    cycleCounterInit();

    // load the unique id into a local array
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    memcpy(&systemUniqueId, &id.id, MIN(sizeof(systemUniqueId), (uint32_t)PICO_UNIQUE_BOARD_ID_SIZE_BYTES));


#ifdef USE_MULTICORE
    multicoreStart();
#endif // USE_MULTICORE
}

void systemResetToBootloader(bootloaderRequestType_e requestType)
{
    switch (requestType) {
    case BOOTLOADER_REQUEST_ROM:
        rom_reset_usb_boot_extra(-1, 0, false);
        break;
    case BOOTLOADER_REQUEST_FLASH:
    default:
        systemReset(RESET_BOOTLOADER_REQUEST_FLASH);
    }
}

// We can make use of time_us_64 if BF defines USE_64BIT_TIME in future, but that will require some changes
STATIC_ASSERT(sizeof(timeMs_t) == sizeof(uint32_t), timeMs_t_is_32_bit_failed);
STATIC_ASSERT(sizeof(timeUs_t) == sizeof(uint32_t), timeUs_t_is_32_bit_failed);

// Return system uptime in milliseconds (rollover in 49 days)
timeMs_t millis(void)
{
    return (timeMs_t)(time_us_64() / 1000);
}

// Return system uptime in micros (rollover in 71 mins)
timeUs_t micros(void)
{
    return time_us_32();
}

timeUs_t microsISR(void)
{
    return micros();
}

void delayMicroseconds(uint32_t us)
{
    sleep_us(us);
}

void delay(uint32_t ms)
{
    sleep_ms(ms);
}

uint32_t getCycleCounter(void)
{
    return PICO_DWT_CYCCNT;
}

// Conversion routines copied from platform/common/stm32/system.c
int32_t clockCyclesToMicros(int32_t clockCycles)
{
    return clockCycles / usTicks;
}

float clockCyclesToMicrosf(int32_t clockCycles)
{
    return clockCycles * usTicksInv;
}

// Note that this conversion is signed as this is used for periods rather than absolute timestamps
int32_t clockCyclesTo10thMicros(int32_t clockCycles)
{
    return 10 * clockCycles / (int32_t)usTicks;
}

// Note that this conversion is signed as this is used for periods rather than absolute timestamps
int32_t clockCyclesTo100thMicros(int32_t clockCycles)
{
    return 100 * clockCycles / (int32_t)usTicks;
}

uint32_t clockMicrosToCycles(uint32_t micros)
{
    return micros * usTicks;
}

static void indicate(uint8_t count, uint16_t duration)
{
    if (count) {
        LED1_ON;
        LED0_OFF;

        while (count--) {
            LED1_TOGGLE;
            LED0_TOGGLE;
            BEEP_ON;
            delay(duration);

            LED1_TOGGLE;
            LED0_TOGGLE;
            BEEP_OFF;
            delay(duration);
        }
    }
}

void indicateFailure(failureMode_e mode, int codeRepeatsRemaining)
{
    while (codeRepeatsRemaining--) {
        indicate(WARNING_FLASH_COUNT, WARNING_FLASH_DURATION_MS);

        delay(WARNING_PAUSE_DURATION_MS);

        indicate(mode + 1, WARNING_CODE_DURATION_LONG_MS);

        delay(1000);
    }
}

void failureMode(failureMode_e mode)
{
    indicateFailure(mode, 10);

#ifdef DEBUG
    systemReset(RESET_BOOTLOADER_REQUEST_ROM);
#else
    systemResetToBootloader(BOOTLOADER_REQUEST_ROM);
#endif
}

static void unusedPinInit(IO_t io)
{
    if (IOGetOwner(io) == OWNER_FREE) {
        IOConfigGPIO(io, 0);
    }
}

void unusedPinsInit(void)
{
    IOTraversePins(unusedPinInit);
}
