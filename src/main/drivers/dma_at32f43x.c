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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_DMA

#include "drivers/nvic.h"
#include "drivers/rcc.h"
#include "dma.h"
#include "resource.h"

/*
 * DMA descriptors.
 *
 * AT32F43x has 7 channels each on DMA1/DMA2, each with its own sts/clr flag registers
 * (unlike STM32's single shared ISR/IFCR per controller), so both controllers restart
 * their flagsShift at bit 0.
 */
dmaChannelDescriptor_t dmaDescriptors[DMA_LAST_HANDLER] = {
    DEFINE_DMA_CHANNEL(DMA1, 1,  0),
    DEFINE_DMA_CHANNEL(DMA1, 2,  4),
    DEFINE_DMA_CHANNEL(DMA1, 3,  8),
    DEFINE_DMA_CHANNEL(DMA1, 4, 12),
    DEFINE_DMA_CHANNEL(DMA1, 5, 16),
    DEFINE_DMA_CHANNEL(DMA1, 6, 20),
    DEFINE_DMA_CHANNEL(DMA1, 7, 24),

    DEFINE_DMA_CHANNEL(DMA2, 1,  0),
    DEFINE_DMA_CHANNEL(DMA2, 2,  4),
    DEFINE_DMA_CHANNEL(DMA2, 3,  8),
    DEFINE_DMA_CHANNEL(DMA2, 4, 12),
    DEFINE_DMA_CHANNEL(DMA2, 5, 16),
    DEFINE_DMA_CHANNEL(DMA2, 6, 20),
    DEFINE_DMA_CHANNEL(DMA2, 7, 24),
};

/*
 * DMA IRQ Handlers
 */
DEFINE_DMA_IRQ_HANDLER(1, 1, DMA1_CH1_HANDLER)
DEFINE_DMA_IRQ_HANDLER(1, 2, DMA1_CH2_HANDLER)
DEFINE_DMA_IRQ_HANDLER(1, 3, DMA1_CH3_HANDLER)
DEFINE_DMA_IRQ_HANDLER(1, 4, DMA1_CH4_HANDLER)
DEFINE_DMA_IRQ_HANDLER(1, 5, DMA1_CH5_HANDLER)
DEFINE_DMA_IRQ_HANDLER(1, 6, DMA1_CH6_HANDLER)
DEFINE_DMA_IRQ_HANDLER(1, 7, DMA1_CH7_HANDLER)
DEFINE_DMA_IRQ_HANDLER(2, 1, DMA2_CH1_HANDLER)
DEFINE_DMA_IRQ_HANDLER(2, 2, DMA2_CH2_HANDLER)
DEFINE_DMA_IRQ_HANDLER(2, 3, DMA2_CH3_HANDLER)
DEFINE_DMA_IRQ_HANDLER(2, 4, DMA2_CH4_HANDLER)
DEFINE_DMA_IRQ_HANDLER(2, 5, DMA2_CH5_HANDLER)
DEFINE_DMA_IRQ_HANDLER(2, 6, DMA2_CH6_HANDLER)
DEFINE_DMA_IRQ_HANDLER(2, 7, DMA2_CH7_HANDLER)

void dmaEnable(dmaIdentifier_e identifier)
{
    const int index = DMA_IDENTIFIER_TO_INDEX(identifier);
    // RCC_AHB1() token-pastes its argument, so it must be applied to a literal DMA1/DMA2
    // token at each branch rather than to a runtime ternary expression.
    RCC_ClockCmd(dmaDescriptors[index].dma == DMA1 ? RCC_AHB1(DMA1) : RCC_AHB1(DMA2), ENABLE);
}

void dmaSetHandler(dmaIdentifier_e identifier, dmaCallbackHandlerFuncPtr callback, uint32_t priority, uint32_t userParam)
{
    const int index = DMA_IDENTIFIER_TO_INDEX(identifier);

    RCC_ClockCmd(dmaDescriptors[index].dma == DMA1 ? RCC_AHB1(DMA1) : RCC_AHB1(DMA2), ENABLE);
    dmaDescriptors[index].irqHandlerCallback = callback;
    dmaDescriptors[index].userParam = userParam;

    nvic_irq_enable(dmaDescriptors[index].irqN, NVIC_PRIORITY_BASE(priority), NVIC_PRIORITY_SUB(priority));
}

void dmaMuxEnable(dmaIdentifier_e identifier, uint32_t dmaMuxId)
{
    const int index = DMA_IDENTIFIER_TO_INDEX(identifier);
    dma_flexible_config((dma_type *)dmaDescriptors[index].dma, dmaDescriptors[index].dmamux, (dmamux_requst_id_sel_type)dmaMuxId);
}

#endif
