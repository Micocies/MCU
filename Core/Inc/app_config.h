#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

/* 采样链路默认参数，集中管理便于后续调参与版本对比。 */
#define APP_SAMPLE_RATE_HZ                1000U
#define APP_BIAS_STABILIZE_MS             100U
#define APP_DARK_CALIBRATION_SAMPLES      256U
#define APP_DRDY_TIMEOUT_MS               10U
#define APP_FILTER_ALPHA_SHIFT            3U
#define APP_USB_QUEUE_DEPTH               16U
#define APP_DAC_BIAS_CH1                  2048U // 12-bit DAC 输出中点，提供约 VREF/2 的偏置电压
#define APP_DAC_BIAS_CH2                  2048U
#define APP_FAULT_REPORT_INTERVAL_MS      1000U

/* TIM6 工作在 1 MHz 计数时基，采样周期可直接由自动重装值推导。 */
#define APP_TIM6_INPUT_CLOCK_HZ           170000000UL
#define APP_TIM6_TICK_HZ                  1000000UL
#define APP_TIM6_PRESCALER                ((APP_TIM6_INPUT_CLOCK_HZ / APP_TIM6_TICK_HZ) - 1UL)
#define APP_TIM6_PERIOD                   ((APP_TIM6_TICK_HZ / APP_SAMPLE_RATE_HZ) - 1UL)

/* ADS1220 普通数据帧固定为 24 bit，即 3 字节。 */
#define APP_ADC_FRAME_BYTES               3U
#define APP_ADC_RESET_PULSE_MS            1U
#define APP_ADC_START_PULSE_CYCLES        64U
#define APP_ADC_SPI_TIMEOUT_INIT_MS       20U
#define APP_ADC_SPI_TIMEOUT_RUN_MS        5U

/* USB 二进制帧包头和状态位定义。 */
#define SAMPLE_PACKET_MAGIC               0xA55AU
#define SAMPLE_PACKET_VERSION             2U

#define SAMPLE_FLAG_DRDY_TIMEOUT          0x0001U
#define SAMPLE_FLAG_SPI_ERROR             0x0002U
#define SAMPLE_FLAG_COMM_CHECK_FAILED     0x0004U
#define SAMPLE_FLAG_USB_OVERFLOW          0x0008U
#define SAMPLE_FLAG_FAULT_STATE           0x0010U
#define SAMPLE_FLAG_FAULT_REPORT          0x0020U
#define SAMPLE_FLAG_INFO_FRAME            0x0040U
#define SAMPLE_FLAG_PARAM_FRAME           0x0080U

#define APP_USB_COMMAND_INFO              'I'
#define APP_USB_COMMAND_PARAMS            'P'
#define APP_USB_COMMAND_BASELINE          'B'

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIG_H */
