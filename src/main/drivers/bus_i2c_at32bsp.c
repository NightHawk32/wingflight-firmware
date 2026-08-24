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

#if defined(USE_I2C) && !defined(SOFT_I2C)

#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "drivers/nvic.h"
#include "drivers/time.h"
#include "drivers/rcc.h"

#include "drivers/bus_i2c.h"
#include "drivers/bus_i2c_impl.h"
#include "drivers/bus_i2c_timing.h"

// Number of bits in I2C protocol phase
#define LEN_ADDR 7
#define LEN_RW 1
#define LEN_ACK 1

// Clock period in us during unstick transfer
#define UNSTICK_CLK_US 10

// Allow 500us for clock strech to complete during unstick
#define UNSTICK_CLK_STRETCH (500/UNSTICK_CLK_US)

static void i2cUnstick(IO_t scl, IO_t sda);

#define IOCFG_I2C_PU IO_CONFIG(GPIO_MODE_MUX, GPIO_DRIVE_STRENGTH_STRONGER, GPIO_OUTPUT_OPEN_DRAIN, GPIO_PULL_UP)
#define IOCFG_I2C    IO_CONFIG(GPIO_MODE_MUX, GPIO_DRIVE_STRENGTH_STRONGER, GPIO_OUTPUT_OPEN_DRAIN, GPIO_PULL_NONE)

#ifdef USE_I2C_DEVICE_1
void I2C1_EVT_IRQHandler(void)
{
    i2c_evt_irq_handler(&i2cDevice[I2CDEV_1].handle);
}

void I2C1_ERR_IRQHandler(void)
{
    i2c_err_irq_handler(&i2cDevice[I2CDEV_1].handle);
}
#endif

#ifdef USE_I2C_DEVICE_2
void I2C2_EVT_IRQHandler(void)
{
    i2c_evt_irq_handler(&i2cDevice[I2CDEV_2].handle);
}

void I2C2_ERR_IRQHandler(void)
{
    i2c_err_irq_handler(&i2cDevice[I2CDEV_2].handle);
}
#endif

#ifdef USE_I2C_DEVICE_3
void I2C3_EVT_IRQHandler(void)
{
    i2c_evt_irq_handler(&i2cDevice[I2CDEV_3].handle);
}

void I2C3_ERR_IRQHandler(void)
{
    i2c_err_irq_handler(&i2cDevice[I2CDEV_3].handle);
}
#endif

// AT32F435/437 has 3 I2C peripherals; pin/AF assignments per the AT32F435 reference manual
const i2cHardware_t i2cHardware[] = {
#ifdef USE_I2C_DEVICE_1
    {
        .device = I2CDEV_1,
        .reg = I2C1,
        .sclPins = {
            I2CPINDEF(PA9,  GPIO_MUX_8),
            I2CPINDEF(PB6,  GPIO_MUX_4),
            I2CPINDEF(PB8,  GPIO_MUX_4),
            I2CPINDEF(PC6,  GPIO_MUX_4),
        },
        .sdaPins = {
            I2CPINDEF(PA10, GPIO_MUX_8),
            I2CPINDEF(PB7,  GPIO_MUX_4),
            I2CPINDEF(PB9,  GPIO_MUX_4),
            I2CPINDEF(PC7,  GPIO_MUX_4),
        },
        .rcc = RCC_APB1(I2C1),
        .ev_irq = I2C1_EVT_IRQn,
        .er_irq = I2C1_ERR_IRQn,
    },
#endif
#ifdef USE_I2C_DEVICE_2
    {
        .device = I2CDEV_2,
        .reg = I2C2,
        .sclPins = {
            I2CPINDEF(PA0,  GPIO_MUX_4),
            I2CPINDEF(PA11, GPIO_MUX_4),
            I2CPINDEF(PB10, GPIO_MUX_4),
            I2CPINDEF(PD12, GPIO_MUX_4),
            I2CPINDEF(PH2,  GPIO_MUX_4),
        },
        .sdaPins = {
            I2CPINDEF(PA1,  GPIO_MUX_4),
            I2CPINDEF(PA12, GPIO_MUX_4),
            I2CPINDEF(PB3,  GPIO_MUX_4),
            I2CPINDEF(PB9,  GPIO_MUX_7),
            I2CPINDEF(PB11, GPIO_MUX_4),
            I2CPINDEF(PC12, GPIO_MUX_4),
            I2CPINDEF(PD13, GPIO_MUX_4),
            I2CPINDEF(PH3,  GPIO_MUX_4),
        },
        .rcc = RCC_APB1(I2C2),
        .ev_irq = I2C2_EVT_IRQn,
        .er_irq = I2C2_ERR_IRQn,
    },
#endif
#ifdef USE_I2C_DEVICE_3
    {
        .device = I2CDEV_3,
        .reg = I2C3,
        .sclPins = {
            I2CPINDEF(PA8,  GPIO_MUX_4),
            I2CPINDEF(PB13, GPIO_MUX_7),
            I2CPINDEF(PB15, GPIO_MUX_4),
            I2CPINDEF(PC0,  GPIO_MUX_4),
            I2CPINDEF(PD14, GPIO_MUX_4),
        },
        .sdaPins = {
            I2CPINDEF(PB4,  GPIO_MUX_4),
            I2CPINDEF(PB14, GPIO_MUX_4),
            I2CPINDEF(PC1,  GPIO_MUX_4),
            I2CPINDEF(PC9,  GPIO_MUX_4),
            I2CPINDEF(PD15, GPIO_MUX_4),
        },
        .rcc = RCC_APB1(I2C3),
        .ev_irq = I2C3_EVT_IRQn,
        .er_irq = I2C3_ERR_IRQn,
    },
#endif
};

i2cDevice_t i2cDevice[I2CDEV_COUNT];

void i2cInit(I2CDevice device)
{
    if (device == I2CINVALID) {
        return;
    }

    i2cDevice_t *pDev = &i2cDevice[device];

    const i2cHardware_t *hardware = pDev->hardware;
    const IO_t scl = pDev->scl;
    const IO_t sda = pDev->sda;

    if (!hardware || IOGetOwner(scl) || IOGetOwner(sda)) {
        return;
    }

    IOInit(scl, OWNER_I2C_SCL, RESOURCE_INDEX(device));
    IOInit(sda, OWNER_I2C_SDA, RESOURCE_INDEX(device));

    // Enable I2C RCC
    RCC_ClockCmd(hardware->rcc, ENABLE);

    i2cUnstick(scl, sda);

    // Init pins
    IOConfigGPIOAF(scl, pDev->pullUp ? IOCFG_I2C_PU : IOCFG_I2C, pDev->sclAF);
    IOConfigGPIOAF(sda, pDev->pullUp ? IOCFG_I2C_PU : IOCFG_I2C, pDev->sdaAF);

    // Init I2C peripheral
    i2c_handle_type *pHandle = &pDev->handle;
    memset(pHandle, 0, sizeof(*pHandle));

    pHandle->i2cx = (i2c_type *)hardware->reg;

    // I2C1-3 all clock from APB1 on AT32F435/437
    crm_clocks_freq_type crmClockFreq;
    crm_clocks_freq_get(&crmClockFreq);

    const uint32_t i2cClkCtrl = i2cClockTIMINGR(crmClockFreq.apb1_freq, pDev->clockSpeed, 0);

    i2c_config(pHandle);
    i2c_init(pHandle->i2cx, 0x0F, i2cClkCtrl);
    i2c_own_address1_set(pHandle->i2cx, I2C_ADDRESS_MODE_7BIT, 0x0);

    nvic_irq_enable(hardware->er_irq, NVIC_PRIORITY_BASE(NVIC_PRIO_I2C_ER), NVIC_PRIORITY_SUB(NVIC_PRIO_I2C_ER));
    nvic_irq_enable(hardware->ev_irq, NVIC_PRIORITY_BASE(NVIC_PRIO_I2C_EV), NVIC_PRIORITY_SUB(NVIC_PRIO_I2C_EV));

    i2c_enable(pHandle->i2cx, TRUE);
}

static void i2cUnstick(IO_t scl, IO_t sda)
{
    int i;

    IOHi(scl);
    IOHi(sda);

    IOConfigGPIO(scl, IOCFG_OUT_OD);
    IOConfigGPIO(sda, IOCFG_OUT_OD);

    // Clock out, with SDA high:
    //   7 data bits
    //   1 READ bit
    //   1 cycle for the ACK
    for (i = 0; i < (LEN_ADDR + LEN_RW + LEN_ACK); i++) {
        // Wait for any clock stretching to finish
        int timeout = UNSTICK_CLK_STRETCH;
        while (!IORead(scl) && timeout) {
            delayMicroseconds(UNSTICK_CLK_US);
            timeout--;
        }

        // Pull low
        IOLo(scl); // Set bus low
        delayMicroseconds(UNSTICK_CLK_US/2);
        IOHi(scl); // Set bus high
        delayMicroseconds(UNSTICK_CLK_US/2);
    }

    // Generate a stop condition in case there was none
    IOLo(scl);
    delayMicroseconds(UNSTICK_CLK_US/2);
    IOLo(sda);
    delayMicroseconds(UNSTICK_CLK_US/2);

    IOHi(scl); // Set bus scl high
    delayMicroseconds(UNSTICK_CLK_US/2);
    IOHi(sda); // Set bus sda high
}

static volatile uint16_t i2cErrorCount = 0;

static bool i2cHandleHardwareFailure(I2CDevice device)
{
    UNUSED(device);
    i2cErrorCount++;
    return false;
}

uint16_t i2cGetErrorCounter(void)
{
    return i2cErrorCount;
}

bool i2cWrite(I2CDevice device, uint8_t addr_, uint8_t reg_, uint8_t data)
{
    if (device == I2CINVALID || device >= I2CDEV_COUNT) {
        return false;
    }

    i2c_handle_type *pHandle = &i2cDevice[device].handle;

    if (!pHandle->i2cx) {
        return false;
    }

    i2c_status_type status;

    if (reg_ == 0xFF) {
        status = i2c_master_transmit(pHandle, addr_ << 1, &data, 1, I2C_TIMEOUT_US);
    } else {
        status = i2c_memory_write(pHandle, I2C_MEM_ADDR_WIDIH_8, addr_ << 1, reg_, &data, 1, I2C_TIMEOUT_US);
    }

    if (status != I2C_OK) {
        i2c_wait_flag(pHandle, I2C_STOPF_FLAG, I2C_EVENT_CHECK_NONE, I2C_TIMEOUT_US);
        i2c_flag_clear(pHandle->i2cx, I2C_STOPF_FLAG);
        return i2cHandleHardwareFailure(device);
    }

    return true;
}

bool i2cWriteBuffer(I2CDevice device, uint8_t addr_, uint8_t reg_, uint8_t len_, uint8_t *data)
{
    if (device == I2CINVALID || device >= I2CDEV_COUNT) {
        return false;
    }

    i2c_handle_type *pHandle = &i2cDevice[device].handle;

    if (!pHandle->i2cx) {
        return false;
    }

    i2c_status_type status = i2c_memory_write_int(pHandle, I2C_MEM_ADDR_WIDIH_8, addr_ << 1, reg_, data, len_, I2C_TIMEOUT_US);

    if (status == I2C_ERR_STEP_1) {
        // Busy - matches the HAL_BUSY non-blocking-write-rejected case on other MCUs
        return false;
    }

    if (status != I2C_OK) {
        i2c_wait_flag(pHandle, I2C_STOPF_FLAG, I2C_EVENT_CHECK_NONE, I2C_TIMEOUT_US);
        i2c_flag_clear(pHandle->i2cx, I2C_STOPF_FLAG);
        return i2cHandleHardwareFailure(device);
    }

    return true;
}

bool i2cRead(I2CDevice device, uint8_t addr_, uint8_t reg_, uint8_t len, uint8_t *buf)
{
    if (device == I2CINVALID || device >= I2CDEV_COUNT) {
        return false;
    }

    i2c_handle_type *pHandle = &i2cDevice[device].handle;

    if (!pHandle->i2cx) {
        return false;
    }

    i2c_status_type status;

    if (reg_ == 0xFF) {
        status = i2c_master_receive(pHandle, addr_ << 1, buf, len, I2C_TIMEOUT_US);
    } else {
        status = i2c_memory_read(pHandle, I2C_MEM_ADDR_WIDIH_8, addr_ << 1, reg_, buf, len, I2C_TIMEOUT_US);
    }

    if (status != I2C_OK) {
        i2c_wait_flag(pHandle, I2C_STOPF_FLAG, I2C_EVENT_CHECK_NONE, I2C_TIMEOUT_US);
        i2c_flag_clear(pHandle->i2cx, I2C_STOPF_FLAG);
        return i2cHandleHardwareFailure(device);
    }

    return true;
}

bool i2cReadBuffer(I2CDevice device, uint8_t addr_, uint8_t reg_, uint8_t len, uint8_t *buf)
{
    if (device == I2CINVALID || device >= I2CDEV_COUNT) {
        return false;
    }

    i2c_handle_type *pHandle = &i2cDevice[device].handle;

    if (!pHandle->i2cx) {
        return false;
    }

    i2c_status_type status = i2c_memory_read_int(pHandle, I2C_MEM_ADDR_WIDIH_8, addr_ << 1, reg_, buf, len, I2C_TIMEOUT_US);

    if (status == I2C_ERR_STEP_1) {
        return false;
    }

    if (status != I2C_OK) {
        i2c_wait_flag(pHandle, I2C_STOPF_FLAG, I2C_EVENT_CHECK_NONE, I2C_TIMEOUT_US);
        i2c_flag_clear(pHandle->i2cx, I2C_STOPF_FLAG);
        return i2cHandleHardwareFailure(device);
    }

    return true;
}

bool i2cBusy(I2CDevice device, bool *error)
{
    i2c_handle_type *pHandle = &i2cDevice[device].handle;

    if (error) {
        *error = pHandle->error_code != I2C_OK;
    }

    // I2C_ERR_ACKFAIL indicates that the last access wasn't acknowledged, but doesn't mean the bus is busy
    if ((pHandle->error_code == I2C_OK) || (pHandle->error_code == I2C_ERR_ACKFAIL)) {
        return i2c_flag_get(pHandle->i2cx, I2C_BUSYF_FLAG) == SET;
    }

    return true;
}

#endif
