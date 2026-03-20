/* ============================================================================
 * 应用层总览
 *
 * 状态机主流程：
 *
 *   上电
 *    |
 *    v
 *   APP_STATE_INIT
 *    |  复位 ADS1220
 *    |  写默认寄存器
 *    |  启动 DAC 偏置
 *    v
 *   APP_STATE_BIAS_STABILIZE
 *    |  等待模拟前端稳定
 *    v
 *   APP_STATE_COMM_CHECK
 *    |  发起一次试采样
 *    v
 *   APP_STATE_WAIT_DRDY
 *    |  等待 DRDY 下降沿
 *    v
 *   APP_STATE_READ_SAMPLE
 *    |  SPI 读取 3 字节
 *    v
 *   APP_STATE_PROCESS_SAMPLE
 *    |  通信正常 -> 进入暗态校准
 *    |  通信异常 -> 进入故障态
 *    v
 *   APP_STATE_DARK_CALIBRATE
 *    |  采集 N 个样本求均值基线
 *    v
 *   APP_STATE_WAIT_TRIGGER <----- TIM6 周期节拍
 *    |                                ^
 *    | 发起转换                        |
 *    v                                |
 *   APP_STATE_WAIT_DRDY --------------+
 *    |
 *    v
 *   APP_STATE_READ_SAMPLE
 *    |
 *    v
 *   APP_STATE_PROCESS_SAMPLE
 *    |  原始值 -> IIR滤波 -> 基线扣除 -> 时间戳
 *    v
 *   APP_STATE_USB_FLUSH
 *    |  结果入 USB 队列
 *    +-------------------------------> 回到 WAIT_TRIGGER
 *
 * 异常路径：
 *   任意阶段发现超时 / SPI错误 / 配置回读失败
 *    -> APP_STATE_FAULT
 *    -> 停止 TIM6、停止 ADC 转换、周期性发送故障帧
 *
 * 模块分工：
 *   main.c         : CubeMX 外设初始化 + 主循环入口
 *   app.c          : 状态机调度、滤波、暗态校准、数据打包
 *   adc_protocol.c : ADS1220 命令、寄存器、读数和码值解析
 *   usb_stream.c   : USB CDC 队列、64字节打包和重试发送
 *   stm32g4xx_it.c : EXTI0 / TIM6 / USB 中断转发
 * ==========================================================================*/

#include "app.h"

#include <string.h>

#include "adc_protocol.h"
#include "app_config.h"
#include "main.h"
#include "usb_stream.h"

extern DAC_HandleTypeDef hdac1;
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim6;

/* 状态机内部还要区分“当前样本是用于自检、校准还是正常运行”。 */
typedef enum
{
  APP_PIPELINE_COMM_CHECK = 0,
  APP_PIPELINE_CALIBRATION,
  APP_PIPELINE_RUN
} app_pipeline_mode_t;

typedef struct
{
  volatile uint8_t evt_sample_tick;
  volatile uint8_t evt_drdy;
  app_state_t state;
  app_pipeline_mode_t pipeline_mode;
  uint32_t state_enter_ms;
  uint32_t drdy_deadline_ms;
  uint32_t last_fault_report_ms;
  uint32_t sequence;
  uint32_t calibration_count;
  int64_t calibration_accumulator;
  int32_t raw_code;
  int32_t filtered_code;
  int32_t baseline_code;
  int32_t corrected_code;
  uint16_t fault_flags;
  uint8_t filter_valid;
} app_context_t;

static app_context_t g_app;

/* 切换状态时统一更新时间戳，便于做超时判断。 */
static void app_set_state(app_state_t state)
{
  g_app.state = state;
  g_app.state_enter_ms = HAL_GetTick();
}

static bool app_has_elapsed(uint32_t start_ms, uint32_t duration_ms)
{
  return ((HAL_GetTick() - start_ms) >= duration_ms);
}

/* 时间戳优先使用 DWT 周期计数器，拿不到时退化到毫秒 tick。 */
static uint32_t app_get_timestamp_us(void)
{
  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
  {
    return HAL_GetTick() * 1000U;
  }

  return (uint32_t)(DWT->CYCCNT / (SystemCoreClock / 1000000U));
}

/* 打开 Cortex-M 周期计数器，用于生成微秒级时间戳。 */
static void app_enable_cycle_counter(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void app_start_timer(void)
{
  __HAL_TIM_SET_COUNTER(&htim6, 0U);
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
  HAL_TIM_Base_Start_IT(&htim6);
}

static void app_stop_timer(void)
{
  HAL_TIM_Base_Stop_IT(&htim6);
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
}

/* 进入故障态时统一收口：停采样、停转换、清事件。 */
static void app_enter_fault(uint16_t reason_flags)
{
  g_app.fault_flags |= reason_flags;
  app_stop_timer();
  adc_protocol_stop();
  g_app.evt_sample_tick = 0U;
  g_app.evt_drdy = 0U;
  g_app.last_fault_report_ms = HAL_GetTick() - APP_FAULT_REPORT_INTERVAL_MS;
  app_set_state(APP_STATE_FAULT);
}

/* 触发一次 ADS1220 转换，并开始等待 DRDY。 */
static void app_begin_conversion(app_pipeline_mode_t pipeline_mode)
{
  g_app.pipeline_mode = pipeline_mode;
  g_app.evt_drdy = 0U;
  adc_protocol_start_conversion();
  g_app.drdy_deadline_ms = HAL_GetTick() + APP_DRDY_TIMEOUT_MS;
  app_set_state(APP_STATE_WAIT_DRDY);
}

/* 第一版使用整数 IIR 低通，计算量小，适合 MCU 常驻运行。 */
static int32_t app_filter_raw_code(int32_t raw_code)
{
  if (g_app.filter_valid == 0U)
  {
    g_app.filter_valid = 1U;
    g_app.filtered_code = raw_code;
  }
  else
  {
    g_app.filtered_code += (raw_code - g_app.filtered_code) / (int32_t)(1UL << APP_FILTER_ALPHA_SHIFT);
  }

  return g_app.filtered_code;
}

/* 把当前处理结果封装成发给上位机的定长二进制帧。 */
static void app_build_packet(sample_packet_t *pkt, uint16_t flags)
{
  memset(pkt, 0, sizeof(*pkt));
  pkt->magic = SAMPLE_PACKET_MAGIC;
  pkt->version = SAMPLE_PACKET_VERSION;
  pkt->state = (uint8_t)g_app.state;
  pkt->flags = flags;
  pkt->sequence = g_app.sequence++;
  pkt->timestamp_us = app_get_timestamp_us();
  pkt->raw_code = g_app.raw_code;
  pkt->filtered_code = g_app.filtered_code;
  pkt->baseline_code = g_app.baseline_code;
  pkt->corrected_code = g_app.corrected_code;
}

/* 故障态不再采样，但仍周期性上报状态，方便 PC 侧诊断。 */
static void app_handle_fault_reporting(void)
{
  sample_packet_t pkt;

  if (!app_has_elapsed(g_app.last_fault_report_ms, APP_FAULT_REPORT_INTERVAL_MS))
  {
    return;
  }

  g_app.last_fault_report_ms = HAL_GetTick();
  app_build_packet(&pkt, (uint16_t)(g_app.fault_flags | SAMPLE_FLAG_FAULT_STATE | SAMPLE_FLAG_FAULT_REPORT));
  (void)usb_stream_enqueue(&pkt);
}

void app_init(void)
{
  memset(&g_app, 0, sizeof(g_app));
  app_enable_cycle_counter();
  usb_stream_init();
  adc_protocol_init(&hspi1);
  app_set_state(APP_STATE_INIT);
}

void app_run_once(void)
{
  sample_packet_t pkt;

  /* 主循环每次都顺带推动 USB 发送，避免队列长期堆积。 */
  usb_stream_service();

  switch (g_app.state)
  {
    case APP_STATE_INIT:
      /* 软件侧的真正启动入口：
       * 复位 ADC、写默认寄存器、启动 DAC 偏置。 */
      app_stop_timer();
      HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_SET);
      adc_protocol_reset();
      if (!adc_protocol_configure_default())
      {
        app_enter_fault(SAMPLE_FLAG_COMM_CHECK_FAILED);
        break;
      }
      if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_1) != HAL_OK)
      {
        app_enter_fault(SAMPLE_FLAG_FAULT_STATE);
        break;
      }
      if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_2) != HAL_OK)
      {
        app_enter_fault(SAMPLE_FLAG_FAULT_STATE);
        break;
      }
      if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, APP_DAC_BIAS_CH1) != HAL_OK)
      {
        app_enter_fault(SAMPLE_FLAG_FAULT_STATE);
        break;
      }
      if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, APP_DAC_BIAS_CH2) != HAL_OK)
      {
        app_enter_fault(SAMPLE_FLAG_FAULT_STATE);
        break;
      }
      app_set_state(APP_STATE_BIAS_STABILIZE);
      break;

    case APP_STATE_BIAS_STABILIZE:
      /* 等待模拟前端与偏置稳定，不做采样。 */
      if (app_has_elapsed(g_app.state_enter_ms, APP_BIAS_STABILIZE_MS))
      {
        app_set_state(APP_STATE_COMM_CHECK);
      }
      break;

    case APP_STATE_COMM_CHECK:
      /* 只做一次试采样，用来验证 SPI、DRDY 和寄存器配置链路。 */
      app_begin_conversion(APP_PIPELINE_COMM_CHECK);
      break;

    case APP_STATE_DARK_CALIBRATE:
      /* 重新清空滤波历史，确保暗态基线不受前序样本污染。 */
      g_app.pipeline_mode = APP_PIPELINE_CALIBRATION;
      g_app.calibration_accumulator = 0;
      g_app.calibration_count = 0U;
      g_app.filter_valid = 0U;
      g_app.baseline_code = 0;
      g_app.corrected_code = 0;
      g_app.fault_flags = 0U;
      app_start_timer();
      app_set_state(APP_STATE_WAIT_TRIGGER);
      break;

    case APP_STATE_WAIT_TRIGGER:
      /* TIM6 中断只置位事件，真正的转换启动放在主循环。 */
      if (g_app.evt_sample_tick != 0U)
      {
        g_app.evt_sample_tick = 0U;
        app_begin_conversion(g_app.pipeline_mode);
      }
      break;

    case APP_STATE_WAIT_DRDY:
      /* DRDY 下降沿表示新数据已进入 ADS1220 内部缓冲。 */
      if (g_app.evt_drdy != 0U)
      {
        g_app.evt_drdy = 0U;
        app_set_state(APP_STATE_READ_SAMPLE);
      }
      else if ((int32_t)(HAL_GetTick() - g_app.drdy_deadline_ms) >= 0)
      {
        app_enter_fault(SAMPLE_FLAG_DRDY_TIMEOUT |
                        ((g_app.pipeline_mode == APP_PIPELINE_COMM_CHECK) ? SAMPLE_FLAG_COMM_CHECK_FAILED : 0U));
      }
      break;

    case APP_STATE_READ_SAMPLE:
      /* SPI 读数统一在主循环完成，避免 ISR 里做阻塞操作。 */
      if (!adc_protocol_read_sample(&g_app.raw_code))
      {
        app_enter_fault(SAMPLE_FLAG_SPI_ERROR |
                        ((g_app.pipeline_mode == APP_PIPELINE_COMM_CHECK) ? SAMPLE_FLAG_COMM_CHECK_FAILED : 0U));
      }
      else
      {
        app_set_state(APP_STATE_PROCESS_SAMPLE);
      }
      break;

    case APP_STATE_PROCESS_SAMPLE:
      /* 当前处理链：原始值 -> IIR 滤波 -> 暗态基线扣除。 */
      (void)app_filter_raw_code(g_app.raw_code);

      if (g_app.pipeline_mode == APP_PIPELINE_COMM_CHECK)
      {
        if (!adc_protocol_link_check(g_app.raw_code))
        {
          app_enter_fault(SAMPLE_FLAG_COMM_CHECK_FAILED);
        }
        else
        {
          app_set_state(APP_STATE_DARK_CALIBRATE);
        }
      }
      else if (g_app.pipeline_mode == APP_PIPELINE_CALIBRATION)
      {
        /* 暗态校准阶段只累计基线，不发 USB 数据。 */
        g_app.calibration_accumulator += g_app.filtered_code;
        g_app.calibration_count++;

        if (g_app.calibration_count >= APP_DARK_CALIBRATION_SAMPLES)
        {
          g_app.baseline_code = (int32_t)(g_app.calibration_accumulator / (int64_t)APP_DARK_CALIBRATION_SAMPLES);
          g_app.filter_valid = 0U;
          g_app.pipeline_mode = APP_PIPELINE_RUN;
        }

        app_set_state(APP_STATE_WAIT_TRIGGER);
      }
      else
      {
        g_app.corrected_code = g_app.filtered_code - g_app.baseline_code;
        app_set_state(APP_STATE_USB_FLUSH);
      }
      break;

    case APP_STATE_USB_FLUSH:
      /* USB 忙时不阻塞采样，先入队，后续由发送服务慢慢发。 */
      app_build_packet(&pkt, 0U);
      (void)usb_stream_enqueue(&pkt);
      app_set_state(APP_STATE_WAIT_TRIGGER);
      break;

    case APP_STATE_FAULT:
      app_handle_fault_reporting();
      break;

    default:
      app_enter_fault(SAMPLE_FLAG_FAULT_STATE);
      break;
  }

  usb_stream_service();
}

void app_on_sample_tick_isr(void)
{
  g_app.evt_sample_tick = 1U;
}

void app_on_drdy_isr(void)
{
  g_app.evt_drdy = 1U;
}
