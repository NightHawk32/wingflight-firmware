/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

// AT32F435/437 ADC driver. Follows the same single-selected-ADC-device architecture as
// drivers/adc_stm32f4xx.c (config->device picks ONE of ADC1/2/3, and every enabled channel
// -- vbat/current/rssi/vbec/vbus/vext -- is wired to that same device), rather than the
// newer per-channel-device model used by betaflight's own current AT32 port (which requires
// a restructured adc.h/adc_impl.h not present in this codebase). USE_ADC_INTERNAL (temp
// sensor/vrefint) is implemented as an ADC1 preempt (injected) channel pair, mirroring
// adc_stm32f4xx.c's ADC_InjectedChannelConfig-based approach 1:1 against AT-BSP's preempt-
// channel API.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_ADC

#include "build/debug.h"

#include "drivers/dma_reqmap.h"
#include "drivers/io.h"
#include "io_impl.h"
#include "rcc.h"
#include "dma.h"

#include "drivers/sensor.h"

#include "adc.h"
#include "adc_impl.h"

#include "pg/adc.h"

#ifdef USE_ADC_INTERNAL
#define ADC_CHANNEL_VREFINT         ADC_CHANNEL_17
#define ADC_CHANNEL_TEMPSENSOR_ADC1 ADC_CHANNEL_16
#endif

const adcDevice_t adcHardware[] = {
    {
        .ADCx = ADC1,
        .rccADC = RCC_APB2(ADC1),
    },
    {
        .ADCx = ADC2,
        .rccADC = RCC_APB2(ADC2),
    },
    {
        .ADCx = ADC3,
        .rccADC = RCC_APB2(ADC3),
    }
};

// Pin/channel data for every ADC1/2/3-capable pin on AT32F435/437 (all packages up to 100 pin)
const adcTagMap_t adcTagMap[] = {
    { DEFIO_TAG_E__PA0, ADC_DEVICES_123, ADC_CHANNEL_0  },
    { DEFIO_TAG_E__PA1, ADC_DEVICES_123, ADC_CHANNEL_1  },
    { DEFIO_TAG_E__PA2, ADC_DEVICES_123, ADC_CHANNEL_2  },
    { DEFIO_TAG_E__PA3, ADC_DEVICES_123, ADC_CHANNEL_3  },
    { DEFIO_TAG_E__PA4, ADC_DEVICES_12,  ADC_CHANNEL_4  },
    { DEFIO_TAG_E__PA5, ADC_DEVICES_12,  ADC_CHANNEL_5  },
    { DEFIO_TAG_E__PA6, ADC_DEVICES_12,  ADC_CHANNEL_6  },
    { DEFIO_TAG_E__PA7, ADC_DEVICES_12,  ADC_CHANNEL_7  },
    { DEFIO_TAG_E__PB0, ADC_DEVICES_12,  ADC_CHANNEL_8  },
    { DEFIO_TAG_E__PB1, ADC_DEVICES_12,  ADC_CHANNEL_9  },
    { DEFIO_TAG_E__PC0, ADC_DEVICES_123, ADC_CHANNEL_10 },
    { DEFIO_TAG_E__PC1, ADC_DEVICES_123, ADC_CHANNEL_11 },
    { DEFIO_TAG_E__PC2, ADC_DEVICES_123, ADC_CHANNEL_12 },
    { DEFIO_TAG_E__PC3, ADC_DEVICES_123, ADC_CHANNEL_13 },
    { DEFIO_TAG_E__PC4, ADC_DEVICES_12,  ADC_CHANNEL_14 },
    { DEFIO_TAG_E__PC5, ADC_DEVICES_12,  ADC_CHANNEL_15 },
};

// Intermediate DMA target: AT-BSP's odt (ordinary data register) is transferred as a full
// 32-bit word per betaflight's own working AT32F43x reference; adcGetChannelValues() then
// narrows each active channel's value into the shared 16-bit adcValues[] used by adc.c.
static volatile DMA_DATA uint32_t adcConversionBuffer[ADC_CHANNEL_COUNT];

#ifdef USE_ADC_INTERNAL
// Configure ADC1's preempt (injected) channel group for vrefint/tempsensor, sampled
// independently of the DMA-scanned ordinary channels above -- same role as
// adc_stm32f4xx.c's adcInitInternalInjected().
static void adcInitInternalInjected(const adcConfig_t *config)
{
    UNUSED(config);

    adc_preempt_channel_length_set(ADC1, 2);
    adc_preempt_channel_set(ADC1, ADC_CHANNEL_VREFINT, 1, ADC_SAMPLETIME_640_5);
    adc_preempt_channel_set(ADC1, ADC_CHANNEL_TEMPSENSOR_ADC1, 2, ADC_SAMPLETIME_640_5);
    // Edge = NONE disables hardware triggering; conversions are started purely by
    // adc_preempt_software_trigger_enable() below, so the trigger source is irrelevant.
    adc_preempt_conversion_trigger_set(ADC1, ADC_PREEMPT_TRIG_TMR1CH4, ADC_PREEMPT_TRIG_EDGE_NONE);

    // AT32F435/437 has no factory-programmed OTP calibration words for vrefint/tempsensor
    // (unlike STM32); use fixed nominal datasheet constants instead, matching betaflight's
    // own AT32 ADC driver's setScalingFactors() exactly (see adc_impl.h for the source).
    adcVREFINTCAL = VREFINT_EXPECTED;
    adcTSCAL1 = lrintf((TEMPSENSOR_CAL1_V * 4095.0f) / 3.3f);
    adcTSSlopeK = lrintf(-3300.0f * 1000.0f / 4095.0f / TEMPSENSOR_SLOPE);
}

static bool adcInternalConversionInProgress = false;

bool adcInternalIsBusy(void)
{
    if (adcInternalConversionInProgress) {
        if (adc_flag_get(ADC1, ADC_PCCE_FLAG) != RESET) {
            adcInternalConversionInProgress = false;
        }
    }

    return adcInternalConversionInProgress;
}

void adcInternalStartConversion(void)
{
    adc_flag_clear(ADC1, ADC_PCCE_FLAG);
    adc_preempt_software_trigger_enable(ADC1, TRUE);

    adcInternalConversionInProgress = true;
}

uint16_t adcInternalReadVrefint(void)
{
    return adc_preempt_conversion_data_get(ADC1, ADC_PREEMPT_CHANNEL_1);
}

uint16_t adcInternalReadTempsensor(void)
{
    return adc_preempt_conversion_data_get(ADC1, ADC_PREEMPT_CHANNEL_2);
}
#endif // USE_ADC_INTERNAL

void adcInit(const adcConfig_t *config)
{
    uint8_t i;
    uint8_t configuredAdcChannels = 0;

    memset(&adcOperatingConfig, 0, sizeof(adcOperatingConfig));

    if (config->vbat.enabled) {
        adcOperatingConfig[ADC_BATTERY].tag = config->vbat.ioTag;
    }
    if (config->current.enabled) {
        adcOperatingConfig[ADC_CURRENT].tag = config->current.ioTag;
    }
    if (config->rssi.enabled) {
        adcOperatingConfig[ADC_RSSI].tag = config->rssi.ioTag;
    }
    if (config->vbec.enabled) {
        adcOperatingConfig[ADC_VBEC].tag = config->vbec.ioTag;
    }
    if (config->vbus.enabled) {
        adcOperatingConfig[ADC_VBUS].tag = config->vbus.ioTag;
    }
    if (config->vext.enabled) {
        adcOperatingConfig[ADC_VEXT].tag = config->vext.ioTag;
    }

    ADCDevice device = ADC_CFG_TO_DEV(config->device);

    if (device == ADCINVALID) {
        return;
    }

    adcDevice_t adc = adcHardware[device];

    bool adcActive = false;
    for (int i = 0; i < ADC_CHANNEL_COUNT; i++) {
        if (!adcVerifyPin(adcOperatingConfig[i].tag, device)) {
            continue;
        }

        adcActive = true;
        IOInit(IOGetByTag(adcOperatingConfig[i].tag), OWNER_ADC_BATT + i, 0);
        IOConfigGPIO(IOGetByTag(adcOperatingConfig[i].tag), IO_CONFIG(GPIO_MODE_ANALOG, GPIO_DRIVE_STRENGTH_MODERATE, GPIO_OUTPUT_PUSH_PULL, GPIO_PULL_NONE));
        adcOperatingConfig[i].adcChannel = adcChannelByTag(adcOperatingConfig[i].tag);
        adcOperatingConfig[i].dmaIndex = configuredAdcChannels++;
        adcOperatingConfig[i].sampleTime = ADC_SAMPLETIME_640_5;
        adcOperatingConfig[i].enabled = true;
    }

#ifndef USE_ADC_INTERNAL
    if (!adcActive) {
        return;
    }
#endif

    RCC_ClockCmd(adc.rccADC, ENABLE);

    // Common config applies to all three ADCs, so only needs to be applied once
    adc_common_config_type adc_common_struct;
    adc_common_default_para_init(&adc_common_struct);
    adc_common_struct.combine_mode = ADC_INDEPENDENT_MODE;
    adc_common_struct.div = ADC_HCLK_DIV_4;
    adc_common_struct.common_dma_mode = ADC_COMMON_DMAMODE_DISABLE;
    adc_common_struct.common_dma_request_repeat_state = FALSE;
    adc_common_struct.sampling_interval = ADC_SAMPLING_INTERVAL_5CYCLES;
#ifdef USE_ADC_INTERNAL
    adc_common_struct.tempervintrv_state = TRUE;    // internal channels need to be enabled before use
#endif
    adc_common_config(&adc_common_struct);

#ifdef USE_ADC_INTERNAL
    // ADC1 hosts vrefint/tempsensor regardless of which device the external channels above
    // use; give it its own clock/base-config/enable/calibration if it isn't already the
    // selected device, mirroring adc_stm32f4xx.c's "if (device != ADCDEV_1 || !adcActive)"
    // separate-ADC1-init handling.
    if (device != ADCDEV_1) {
        RCC_ClockCmd(adcHardware[ADCDEV_1].rccADC, ENABLE);

        adc_base_config_type adc1_base_struct;
        adc_base_default_para_init(&adc1_base_struct);
        adc_base_config(ADC1, &adc1_base_struct);
        adc_resolution_set(ADC1, ADC_RESOLUTION_12B);

        adc_enable(ADC1, TRUE);
        while (adc_flag_get(ADC1, ADC_RDY_FLAG) == RESET);

        adc_calibration_init(ADC1);
        while (adc_calibration_init_status_get(ADC1) == SET);
        adc_calibration_start(ADC1);
        while (adc_calibration_status_get(ADC1) == SET);
    }

    adcInitInternalInjected(config);

    if (!adcActive) {
        return;
    }
#endif

    adc_base_config_type adc_base_struct;
    adc_base_default_para_init(&adc_base_struct);
    adc_base_struct.sequence_mode = TRUE;             // reading multiple channels
    adc_base_struct.repeat_mode = TRUE;                // free running, so no need to retrigger
    adc_base_struct.data_align = ADC_RIGHT_ALIGNMENT;
    adc_base_struct.ordinary_channel_length = configuredAdcChannels;
    adc_base_config(adc.ADCx, &adc_base_struct);
    adc_resolution_set(adc.ADCx, ADC_RESOLUTION_12B);

    // Oversample to improve accuracy at the long sample times used for slow-changing inputs
    adc_ordinary_oversample_enable(adc.ADCx, TRUE);
    adc_oversample_ratio_shift_set(adc.ADCx, ADC_OVERSAMPLE_RATIO_4, ADC_OVERSAMPLE_SHIFT_2);

    uint8_t rank = 1;
    for (i = 0; i < ADC_CHANNEL_COUNT; i++) {
        if (!adcOperatingConfig[i].enabled) {
            continue;
        }
        adc_ordinary_channel_set(adc.ADCx, adcOperatingConfig[i].adcChannel, rank++, adcOperatingConfig[i].sampleTime);
    }

    adc_dma_mode_enable(adc.ADCx, TRUE);
    adc_dma_request_repeat_enable(adc.ADCx, TRUE);

    const dmaChannelSpec_t *dmaSpec = dmaGetChannelSpecByPeripheral(DMA_PERIPH_ADC, device, config->dmaopt[device]);

    if (!dmaSpec || !dmaAllocate(dmaGetIdentifier(dmaSpec->ref), OWNER_ADC, RESOURCE_INDEX(device))) {
        return;
    }

    dmaEnable(dmaGetIdentifier(dmaSpec->ref));
    xDMA_DeInit(dmaSpec->ref);

    dma_init_type dma_init_struct;
    dma_default_para_init(&dma_init_struct);
    dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
    dma_init_struct.loop_mode_enable = TRUE;    // circular buffer, no interrupt handling required
    dma_init_struct.peripheral_base_addr = (uint32_t)&adc.ADCx->odt;
    dma_init_struct.peripheral_inc_enable = FALSE;
    dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_WORD;
    dma_init_struct.memory_base_addr = (uint32_t)adcConversionBuffer;
    dma_init_struct.memory_inc_enable = configuredAdcChannels > 1 ? TRUE : FALSE;
    dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_WORD;
    dma_init_struct.buffer_size = configuredAdcChannels;
    dma_init_struct.priority = DMA_PRIORITY_HIGH;

    xDMA_Init(dmaSpec->ref, &dma_init_struct);
    dmaMuxEnable(dmaGetIdentifier(dmaSpec->ref), dmaSpec->channel);
    xDMA_Cmd(dmaSpec->ref, TRUE);

    adc_enable(adc.ADCx, TRUE);
    while (adc_flag_get(adc.ADCx, ADC_RDY_FLAG) == RESET);

    adc_calibration_init(adc.ADCx);
    while (adc_calibration_init_status_get(adc.ADCx) == SET);
    adc_calibration_start(adc.ADCx);
    while (adc_calibration_status_get(adc.ADCx) == SET);

    adc_ordinary_software_trigger_enable(adc.ADCx, TRUE);
}

void adcGetChannelValues(void)
{
    for (int i = 0; i < ADC_CHANNEL_COUNT; i++) {
        if (adcOperatingConfig[i].enabled) {
            adcValues[adcOperatingConfig[i].dmaIndex] = (uint16_t)adcConversionBuffer[adcOperatingConfig[i].dmaIndex];
        }
    }
}
#endif // USE_ADC
