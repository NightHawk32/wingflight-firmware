/*
 * This file is part of Wingflight.
 *
 * Wingflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Wingflight is distributed in the hope that it will be useful,
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

/*
 * Frequency (RPM) sensor input for PICO - the RP2350 counterpart of
 * drivers/freq.c.
 *
 * The RP2350 has no timer input capture. Each edge raises a GPIO interrupt
 * (drivers/exti_pico.c) that timestamps it with the Cortex-M33 DWT cycle
 * counter, which runs at the core clock (150MHz) and is 32 bits wide - the
 * same resolution and width as the STM32 driver's 32-bit timer path
 * (TIM2/TIM5), so the filtering below is that path unchanged. Interrupt
 * latency adds some jitter to single periods, which the period/frequency
 * filters smooth out.
 */

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "platform.h"

#ifdef USE_FREQ_SENSOR

#include "build/debug.h"

#include "common/utils.h"

#include "drivers/exti.h"
#include "drivers/freq.h"
#include "drivers/io.h"
#include "drivers/nvic.h"
#include "drivers/system.h"

#include "pg/freq.h"

// Timeout for missing signal [40ms]
#define FREQ_TIMEOUT(clk)     ((clk)/25)

// Period init value
#define FREQ_PERIOD_INIT      0x2000

// Input signal max deviation from average 66%..150%
#define FREQ_PERIOD_MIN(p)    ((uint32_t)(p)*2/3)
#define FREQ_PERIOD_MAX(p)    ((uint32_t)(p)*3/2)

#define UPDATE_FREQ_FILTER(_input,_freq) \
    ((_input)->freq += ((_freq) - ((_input)->freq)) / ((_input)->freqcoef))

#define UPDATE_PERIOD_FILTER(_input,_period) \
    ((_input)->period += ((int32_t)(_period) - (int32_t)((_input)->period)) / ((_input)->percoef))

typedef struct {

    bool enabled;
    volatile bool captured;

    volatile float freq;
    float clock;

    float minhz;
    float maxhz;

    uint16_t percoef;
    uint16_t freqcoef;

    uint32_t period;
    volatile uint32_t capture;

    uint32_t timeout;

    extiCallbackRec_t edgeCb;

    IO_t pin;

} freqInputPort_t;

static freqInputPort_t freqInputPorts[FREQ_SENSOR_PORT_COUNT];

// Same filter coefficients as drivers/freq.c
static const uint8_t perCoeffs[32] = {
     1,   1,   1,   1,
     1,   1,   1,   1,
     2,   2,   2,   2,
     2,   4,   6,   8,
     8,   8,   8,   8,
     8,   8,   8,   8,
     8,   8,   8,   8,
     8,   8,   8,   8,
};

static const uint8_t freqCoeffs[32] = {
     1,   1,   1,   1,
     1,   1,   1,   1,
     1,   1,   1,   1,
     2,   4,   6,   8,
     8,   8,   8,   8,
     8,   8,   8,   8,
     8,   8,   8,   8,
     8,   8,   8,   8,
};

static void freqResetCapture(freqInputPort_t *input, uint8_t port)
{
    input->period = FREQ_PERIOD_INIT;
    input->captured = false;

    input->percoef = 1;
    input->freqcoef = 1;

    if (port == debugAxis) {
        for (int i = 0; i < 4; i++)
            DEBUG(FREQ_SENSOR, i, 0);
    }
}

static void freqEdgeCallback(extiCallbackRec_t *cb)
{
    freqInputPort_t *input = container_of(cb, freqInputPort_t, edgeCb);

    const uint32_t capture = getCycleCounter();

    if (input->enabled) {
        if (input->captured) {
            const uint32_t period = capture - input->capture;
            if (period) {
                float freq = input->clock / period;
                if (period > FREQ_PERIOD_MIN(input->period) && period < FREQ_PERIOD_MAX(input->period)) {
                    if (freq < input->minhz)
                        freq = 0;
                    if (freq < input->maxhz)
                        UPDATE_FREQ_FILTER(input, freq);
                }

                UPDATE_PERIOD_FILTER(input, period);

                const uint8_t zeros = __builtin_clz(input->period);
                input->percoef = perCoeffs[zeros];
                input->freqcoef = freqCoeffs[zeros];

                const uint8_t port = input - freqInputPorts;
                if (port == debugAxis) {
                    DEBUG(FREQ_SENSOR, 0, input->freq * 1000);
                    DEBUG(FREQ_SENSOR, 1, freq * 1000);
                    DEBUG(FREQ_SENSOR, 2, input->period);
                    DEBUG(FREQ_SENSOR, 3, period);
                    DEBUG(FREQ_SENSOR, 4, zeros);
                }
            }
        }

        input->capture = capture;
        input->captured = true;
    }
}

void freqInit(const freqConfig_t *freqConfig)
{
    for (int port = 0; port < FREQ_SENSOR_PORT_COUNT; port++) {
        const IO_t io = IOGetByTag(freqConfig->ioTag[port]);
        if (!io || !IOIsFreeOrPreinit(io)) {
            continue;
        }

        freqInputPort_t *input = &freqInputPorts[port];

        input->pin = io;
        input->clock = SystemCoreClock;
        input->timeout = FREQ_TIMEOUT(SystemCoreClock);
        input->minhz = freqConfig->minhz;
        input->maxhz = FREQ_INPUT_MAXHZ_DEFAULT;
        input->freq = 0;
        freqResetCapture(input, port);

        IOInit(io, OWNER_FREQ, RESOURCE_INDEX(port));

        const ioConfig_t iocfg =
            (freqConfig->pullupdn == FREQ_INPUT_PULLUP) ? IOCFG_IPU :
            (freqConfig->pullupdn == FREQ_INPUT_PULLDOWN) ? IOCFG_IPD :
            IOCFG_IN_FLOATING;

        const extiTrigger_t trigger = (freqConfig->polarity == FREQ_INPUT_RISING_EDGE) ?
            BETAFLIGHT_EXTI_TRIGGER_RISING : BETAFLIGHT_EXTI_TRIGGER_FALLING;

        EXTIHandlerInit(&input->edgeCb, freqEdgeCallback);
        EXTIConfig(io, &input->edgeCb, NVIC_PRIO_TIMER, iocfg, trigger);

        input->enabled = true;
        EXTIEnable(io);
    }
}

void freqUpdate(void)
{
    const uint32_t now = getCycleCounter();

    for (int port = 0; port < FREQ_SENSOR_PORT_COUNT; port++) {
        freqInputPort_t *input = &freqInputPorts[port];
        if (input->enabled && input->captured) {
            if (now - input->capture > input->timeout) {
                input->freq = 0;
                freqResetCapture(input, port);
            }
        }
    }
}

float getFreqSensorFreq(uint8_t port)
{
    if (port < FREQ_SENSOR_PORT_COUNT) {
        return freqInputPorts[port].freq;
    }
    return 0;
}

uint32_t getFreqSensorRPM(uint8_t port)
{
    if (port < FREQ_SENSOR_PORT_COUNT) {
        return lrintf(freqInputPorts[port].freq * 60);
    }
    return 0;
}

bool isFreqSensorPortInitialized(uint8_t port)
{
    if (port < FREQ_SENSOR_PORT_COUNT) {
        return freqInputPorts[port].enabled;
    }
    return false;
}

bool isFreqSensorInitialized(void)
{
    for (int port = 0; port < FREQ_SENSOR_PORT_COUNT; port++) {
        if (freqInputPorts[port].enabled) {
            return true;
        }
    }
    return false;
}

#endif
