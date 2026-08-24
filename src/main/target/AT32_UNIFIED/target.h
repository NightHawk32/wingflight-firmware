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

#pragma once

/*
 * Common definitions for all AT32 targets
 */

#define USE_UNIFIED_TARGET

#define USE_CUSTOM_DEFAULTS

#define USE_BEEPER

#undef  USE_GYRO_DLPF_EXPERIMENTAL
#define USE_GYRO_CLK

#define USE_ACC
#define USE_GYRO

#undef USE_ACC_MPU6050
#undef USE_GYRO_MPU6050
#undef USE_ACC_MPU6500
#undef USE_GYRO_MPU6500
#define USE_ACC_SPI_MPU6500
#define USE_GYRO_SPI_MPU6500
#define USE_ACC_SPI_MPU6000
#define USE_GYRO_SPI_MPU6000
#define USE_ACC_SPI_ICM20689
#define USE_GYRO_SPI_ICM20689
#undef USE_ACCGYRO_LSM6DSO
#undef USE_ACCGYRO_BMI160
#define USE_ACCGYRO_BMI270
#define USE_ACCGYRO_SPI_BMI323
#define USE_ACCGYRO_SPI_BMI088
#define USE_GYRO_SPI_ICM42605
#define USE_GYRO_SPI_ICM42688P
#define USE_ACC_SPI_ICM42605
#define USE_ACC_SPI_ICM42688P

#define USE_MAG
#define USE_MAG_DATA_READY_SIGNAL
#define USE_MAG_HMC5883
#define USE_MAG_SPI_HMC5883
#define USE_MAG_QMC5883
#define USE_MAG_LIS3MDL
#define USE_MAG_AK8963
#define USE_MAG_MPU925X_AK8963
#define USE_MAG_SPI_AK8963
#define USE_MAG_AK8975

#define USE_BARO
#define USE_BARO_MS5611
#define USE_BARO_SPI_MS5611
#define USE_BARO_BMP085
#define USE_BARO_BMP280
#define USE_BARO_SPI_BMP280
#define USE_BARO_BMP388
#define USE_BARO_SPI_BMP388
#define USE_BARO_LPS
#define USE_BARO_SPI_LPS
#define USE_BARO_QMP6988
#define USE_BARO_SPI_QMP6988
#define USE_BARO_DPS310
#define USE_BARO_SPI_DPS310
#define USE_BARO_BMP581
#define USE_BARO_SPI_BMP581

#define USE_SDCARD
#define USE_SDCARD_SPI

#define USE_FLASHFS
#define USE_FLASHFS_LOOP
#define USE_FLASH_TOOLS
#define USE_FLASH_M25P16
#define USE_FLASH_W25N01G
#define USE_FLASH_W25M
#define USE_FLASH_W25M512
#define USE_FLASH_W25M02G
#define USE_FLASH_W25Q128FV

#define USE_SPI
#define SPI_FULL_RECONFIGURABILITY

#define USE_I2C
#define I2C_FULL_RECONFIGURABILITY

#define USE_VCP

#define USE_SOFTSERIAL1
#define USE_SOFTSERIAL2

#define UNIFIED_SERIAL_PORT_COUNT       3

#define USE_USB_DETECT

#define USE_ESCSERIAL

#define USE_ADC

// drivers/freq.c (input-capture based frequency counter, e.g. for tachometer/RPM sensing
// pins) is now ported for AT32F43x via additive branches (freqICConfig() reuses
// timer_at32bsp.c's timerChConfigIC(), freqSetBaseClock() uses AT-BSP's div/pr/swevt
// registers) -- see AT32F435_TODO.md.

#define USE_SERVO_GEOMETRY_CORRECTION

#undef USE_CRSF_V3

#undef USE_RCDEVICE
#undef USE_VTX_COMMON
#undef USE_VTX_CONTROL
#undef USE_VTX_SMARTAUDIO
#undef USE_VTX_TRAMP
#undef USE_CAMERA_CONTROL

#undef USE_RX_FRSKY_SPI_D
#undef USE_RX_FRSKY_SPI_X
#undef USE_RX_SFHSS_SPI
#undef USE_RX_REDPINE_SPI
#undef USE_RX_FRSKY_SPI_TELEMETRY
#undef USE_RX_CC2500_SPI_PA_LNA
#undef USE_RX_CC2500_SPI_DIVERSITY
#undef USE_RX_FLYSKY
#undef USE_RX_FLYSKY_SPI_LED
#undef USE_RX_SPEKTRUM
#undef USE_RX_SPEKTRUM_TELEMETRY
#undef USE_RX_EXPRESSLRS
#undef USE_RX_SX1280
#undef USE_RX_SX127X


/*
 * AT32F435
 */

#if defined(AT32F435)

#define TARGET_BOARD_IDENTIFIER "A435"

#define USBD_PRODUCT_STRING     "Wingflight AT32F435"

#define USE_I2C_DEVICE_1
#define USE_I2C_DEVICE_2
#define USE_I2C_DEVICE_3

#define USE_UART1
#define USE_UART2
#define USE_UART3
#define USE_UART4
#define USE_UART5
#define USE_UART6
#define USE_UART7
#define USE_UART8

#define SERIAL_PORT_COUNT       (UNIFIED_SERIAL_PORT_COUNT + 8)

#define USE_SPI_DEVICE_1
#define USE_SPI_DEVICE_2
#define USE_SPI_DEVICE_3
#define USE_SPI_DEVICE_4

#define TARGET_IO_PORTA 0xffff
#define TARGET_IO_PORTB 0xffff
#define TARGET_IO_PORTC 0xffff
#define TARGET_IO_PORTD 0xffff
#define TARGET_IO_PORTE 0xffff
#define TARGET_IO_PORTF 0xffff
#define TARGET_IO_PORTG 0xffff
#define TARGET_IO_PORTH 0xffff

#define VOLTAGE_TASK_FREQ_HZ     100
#define CURRENT_TASK_FREQ_HZ     100
#define ESC_SENSOR_TASK_FREQ_HZ  100

#define DEFAULT_FEATURES         (FEATURE_DYN_NOTCH)

/*
 * UNIT_TEST/SDCARD_SDIO/ONBOARDFLASH availability, DMA/timer resource maps and other
 * AT32F435-specific driver support are not implemented yet - see AT32F435_TODO.md.
 */

/*
 * UNKNOWN target
 */

#elif !defined(UNIT_TEST)
#error "No resources defined for this Unified Target."
#endif
