#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 应用层主状态机。
 * 负责把上电初始化、偏置稳定、暗态校准、采样处理和 USB 发送
 * 串成单一的软件流程。 */
typedef enum
{
  /* 上电后的软件上下文初始化和外设准备。 */
  APP_STATE_INIT = 0,
  /* 等待模拟前端与DAC偏置稳定 */
  APP_STATE_BIAS_STABILIZE,
  /* 做一次最小通信自检，确认 ADS1220 可访问。 */
  APP_STATE_COMM_CHECK,
  /* 采集固定数量样本，建立暗态基线。 */
  APP_STATE_DARK_CALIBRATE,
  /* 等待TIM6采样节拍 */
  APP_STATE_WAIT_TRIGGER,
  /* 已发起转换，等待 DRDY 下降沿。 */
  APP_STATE_WAIT_DRDY,
  /* 从ADS1220读出3字节原始数据 */
  APP_STATE_READ_SAMPLE,
  /* 做滤波、基线扣除和结果整理 */
  APP_STATE_PROCESS_SAMPLE,
  /* 把结果封装成二进制帧并送入USB队列 */
  APP_STATE_USB_FLUSH,
  /* 执行一次自动恢复动作，成功后回到启动/采样链路。 */
  APP_STATE_RECOVER,
  /* 进入故障保持态，只上报状态，等待人工干预或复位。 */
  APP_STATE_FAULT
} app_state_t;

/* 初始化应用层上下文，不直接进入采样。 */
void app_init(void);
/* 主循环调度入口，每次调用推进状态机。 */
void app_run_once(void);
/* TIM6 中断只调用这个函数置位采样事件。 */
void app_on_sample_tick_isr(void);
/* DRDY 外部中断只调用这个函数置位数据就绪事件。 */
void app_on_drdy_isr(void);
/* USB CDC 接收最小命令入口，当前识别 I/P/B 三类查询命令。 */
void app_on_usb_command_rx(const uint8_t *data, uint32_t length);

#ifdef UNIT_TEST
/* UNIT_TEST 下的只读观察口，正式固件构建中不暴露这些接口。 */
app_state_t app_test_get_state(void);
uint16_t app_test_get_fault_flags(void);
int32_t app_test_get_baseline_code(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */

