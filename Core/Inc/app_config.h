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
#define APP_USB_QUEUE_DEPTH               16U // USB 样本队列深度，过小可能导致数据丢失过多，过大则占用更多 RAM 并增加单帧延迟
#define APP_USB_AUX_QUEUE_DEPTH           4U
#define APP_USB_BATCH_MAX_WAIT_MS         2U
#define APP_DAC_BIAS_CH1                  2048U // 12-bit DAC 输出中点，提供约 VREF/2 的偏置电压
#define APP_DAC_BIAS_CH2                  2048U
#define APP_FAULT_REPORT_INTERVAL_MS      1000U
#define APP_RECOVERY_SPI_RETRY_LIMIT      3U
#define APP_RECOVERY_RECONFIGURE_LIMIT    3U
#define APP_RECOVERY_HOLD_THRESHOLD       3U

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
#define APP_ADC_LINK_CHECK_RETRIES        2U

/* USB 二进制帧包头和状态位定义。 */
#define SAMPLE_PACKET_MAGIC               0xA55AU
#define SAMPLE_PACKET_VERSION             3U

#define SAMPLE_FLAG_DRDY_TIMEOUT          0x0001U   // 采样等待 DRDY 超时，可能的原因包括 ADC 死锁、SPI 通信异常等
#define SAMPLE_FLAG_SPI_ERROR             0x0002U   // SPI 传输错误，可能的原因包括硬件连接问题、时序配置错误等
#define SAMPLE_FLAG_COMM_CHECK_FAILED     0x0004U   // 自检通信异常，可能的原因包括 ADC 无响应、寄存器读回值不符等
#define SAMPLE_FLAG_USB_OVERFLOW          0x0008U   // USB 队列溢出，表示在当前帧入队时队列已满，最旧数据被丢弃以腾出空间
#define SAMPLE_FLAG_FAULT_STATE           0x0010U   // 故障状态，表示当前样本处于某种异常状态，具体原因需结合其他标志位分析
#define SAMPLE_FLAG_FAULT_REPORT          0x0020U   // 故障报告帧，表示当前帧为周期性故障报告，用于持续上报故障状态以便上位机监控
#define SAMPLE_FLAG_INFO_FRAME            0x0040U   // 信息帧，表示当前帧负载的是版本、构建信息等元数据，而非正常样本数据
#define SAMPLE_FLAG_PARAM_FRAME           0x0080U   // 参数帧，表示当前帧负载的是采样参数、编译时配置等元数据，而非正常样本数据
#define SAMPLE_FLAG_CONFIG_MISMATCH       0x0100U   // ADS1220 配置寄存器回读值与期望镜像不一致
#define SAMPLE_FLAG_SPI_TIMEOUT           0x0200U   // SPI 事务超时，区别于普通 SPI 错误
#define SAMPLE_FLAG_RECOVERY_ACTIVE       0x0400U   // 当前帧与一次自动恢复动作相关
#define SAMPLE_FLAG_RECOVERY_FAILED       0x0800U   // 连续恢复失败，已进入故障保持或即将保持
#define SAMPLE_FLAG_DIAG_FRAME            0x1000U   // 诊断帧，负载为复位原因、故障计数或恢复状态

#define APP_USB_COMMAND_INFO              'I'
#define APP_USB_COMMAND_PARAMS            'P'
#define APP_USB_COMMAND_BASELINE          'B'

#ifdef __cplusplus
}
#endif

#endif /* __APP_CONFIG_H */
