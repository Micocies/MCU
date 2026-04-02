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
 *    |  通信异常 -> 进入故障
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
 *    |  原始值 -> IIR 滤波 -> 基线扣除 -> 时间戳
 *    v
 *   APP_STATE_USB_FLUSH
 *    |  结果入 USB 队列
 *    +-------------------------------> 回到 WAIT_TRIGGER
 *
 * 异常路径：
 *   任意阶段发现超时、SPI 错误或配置回读失败
 *    -> APP_STATE_FAULT
 *    -> 停止 TIM6、停止 ADC 转换、周期性发送故障帧
 *
 * 模块分工：
 *   main.c         : CubeMX 外设初始化 + 主循环入口
 *   app.c          : 状态机调度、滤波、暗态校准、数据打包
 *   adc_protocol.c : ADS1220 命令、寄存器、读数和码值解析
 *   usb_stream.c   : USB CDC 队列、64 字节打包和重试发送
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
  APP_PIPELINE_COMM_CHECK = 0,  //自检
  APP_PIPELINE_CALIBRATION,     //校准
  APP_PIPELINE_RUN              //正常运行
} app_pipeline_mode_t;

typedef struct
{
  volatile uint8_t evt_sample_tick; // TIM6 采样节拍事件
  volatile uint8_t evt_drdy;        // ADS1220 DRDY 事件
  app_state_t state;                // 当前状态
  app_pipeline_mode_t pipeline_mode;// 当前样本所属的流水线阶段
  uint32_t state_enter_ms;          // 进入当前状态的时间戳，单位 ms
  uint32_t drdy_deadline_ms;        // 等待 DRDY 的截止时间戳，单位 ms
  uint32_t last_fault_report_ms;    // 上次故障帧发送的时间戳，单位 ms
  uint32_t sequence;                // 样本序列号，单调递增
  uint32_t calibration_count;       // 已累积的校准样本数量
  int64_t calibration_accumulator;  // 校准样本累加器，用于求暗态基线
  int32_t raw_code;                 // 当前原始 ADC 码值
  int32_t filtered_code;            // 当前滤波结果
  int32_t baseline_code;            // 当前基线值（暗态均值）
  int32_t corrected_code;           // 当前扣除基线后的码值
  uint16_t fault_flags;             // 当前故障标志位，按位定义见 SAMPLE_FLAG_XXX
  uint8_t filter_valid;             // 滤波器是否已初始化
  uint8_t last_adc_status;          // 最近一次 ADC 协议层状态码
  uint8_t last_usb_status;          // 最近一次 USB 入队结果码
} app_context_t;                    // 应用状态机上下文

static app_context_t g_app;

/* 函数说明：
 *   切换应用状态，并刷新该状态的进入时刻。
 * 输入：
 *   state: 目标状态。
 * 输出：
 *   无。
 * 作用：
 *   给状态机统一维护状态切换时间基准。
 */
static void app_set_state(app_state_t state)
{
  g_app.state = state;
  g_app.state_enter_ms = HAL_GetTick();
}

/* 函数说明：
 *   判断从起始时刻到现在是否已经超过指定时长。
 * 输入：
 *   start_ms: 起始时间戳，单位 ms。
 *   duration_ms: 目标时长，单位 ms。
 * 输出：
 *   true : 已达到或超过目标时长。
 *   false: 尚未达到目标时长。
 * 作用：
 *   用于状态机中的超时与等待判断。
 */
static bool app_has_elapsed(uint32_t start_ms, uint32_t duration_ms)
{
  return ((HAL_GetTick() - start_ms) >= duration_ms);
}

/* 函数说明：
 *   获取当前时间戳。
 * 输入：
 *   无。
 * 输出：
 *   返回当前时间戳，单位 us。
 * 作用：
 *   优先使用 DWT 周期计数器生成微秒级时间戳，若不可用则退化为毫秒 tick。
 */
static uint32_t app_get_timestamp_us(void)
{
  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
  {
    return HAL_GetTick() * 1000U;
  }

  return (uint32_t)(DWT->CYCCNT / (SystemCoreClock / 1000000U));
}

/* 函数说明：
 *   使能 DWT 周期计数器。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   为微秒级时间戳提供硬件计数基础。
 */
static void app_enable_cycle_counter(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // 使能 DWT 访问权限
  DWT->CYCCNT = 0U;                               // 复位周期计数器    
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;            // 使能周期计数器
}

/* 函数说明：
 *   启动 TIM6 周期中断。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   作为采样状态机的统一节拍源。
 */
static void app_start_timer(void)
{
  __HAL_TIM_SET_COUNTER(&htim6, 0U);
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
  HAL_TIM_Base_Start_IT(&htim6);
}

/* 函数说明：
 *   停止 TIM6 周期中断。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   终止后续采样节拍。
 */
static void app_stop_timer(void)
{
  HAL_TIM_Base_Stop_IT(&htim6);
  __HAL_TIM_CLEAR_FLAG(&htim6, TIM_FLAG_UPDATE);
}

/* 函数说明：
 *   启动并设置默认前端偏置。
 * 输入：
 *   无。
 * 输出：
 *   true : 成功。
 *   false: 失败。
 * 作用：
 *   收口 DAC 启动和默认偏置设置，减少状态机中的硬件细节散落。
 */
static bool app_frontend_bias_start_default(void)
{
  if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_1) != HAL_OK)
  {
    return false;
  }

  if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_2) != HAL_OK)
  {
    return false;
  }

  if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, APP_DAC_BIAS_CH1) != HAL_OK)
  {
    return false;
  }

  if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, APP_DAC_BIAS_CH2) != HAL_OK)
  {
    return false;
  }

  return true;
}

/* 函数说明：
 *   统一进入故障状态。
 * 输入：
 *   reason_flags: 故障原因标志位。
 * 输出：
 *   无。
 * 作用：
 *   停止节拍与转换，清理事件，并保存故障原因。
 */
static void app_enter_fault(uint16_t reason_flags)
{
  g_app.fault_flags |= reason_flags;
  app_stop_timer();
  g_app.last_adc_status = (uint8_t)adc_protocol_stop();
  g_app.evt_sample_tick = 0U;
  g_app.evt_drdy = 0U;
  g_app.last_fault_report_ms = HAL_GetTick() - APP_FAULT_REPORT_INTERVAL_MS;
  app_set_state(APP_STATE_FAULT);
}

/* 函数说明：
 *   发起一次 ADS1220 转换。
 * 输入：
 *   pipeline_mode: 当前采样属于自检、校准或运行阶段。
 * 输出：
 *   无。
 * 作用：
 *   记录流水线模式，并切换到等待 DRDY 的状态。
 */
static void app_begin_conversion(app_pipeline_mode_t pipeline_mode)
{
  adc_protocol_status_t status;

  g_app.pipeline_mode = pipeline_mode;
  g_app.evt_drdy = 0U;
  status = adc_protocol_start_conversion();
  g_app.last_adc_status = (uint8_t)status;
  if (status != ADC_PROTOCOL_OK)
  {
    app_enter_fault(SAMPLE_FLAG_SPI_ERROR |
                    ((pipeline_mode == APP_PIPELINE_COMM_CHECK) ? SAMPLE_FLAG_COMM_CHECK_FAILED : 0U));
    return;
  }
  g_app.drdy_deadline_ms = HAL_GetTick() + APP_DRDY_TIMEOUT_MS;
  app_set_state(APP_STATE_WAIT_DRDY);
}

/* 函数说明：
 *   更新 IIR 滤波结果。
 * 输入：
 *   raw_code: 当前原始 ADC 码值。
 * 输出：
 *   返回更新后的滤波值。
 * 作用：
 *   使用整数 IIR 低通减少运行态噪声。
 */
static int32_t app_filter_raw_code(int32_t raw_code)
{
  if (g_app.filter_valid == 0U)
  {
    g_app.filter_valid = 1U;
    g_app.filtered_code = raw_code;
  }
  else
  {
    g_app.filtered_code += (raw_code - g_app.filtered_code) / (int32_t)(1UL << APP_FILTER_ALPHA_SHIFT);// APP_FILTER_ALPHA_SHIFT 定义了滤波器的平滑程度，值越大响应越慢但噪声越小。
  }

  return g_app.filtered_code;
}

/* 函数说明：
 *   构造一个样本数据包。
 * 输入：
 *   pkt: 输出数据包指针。
 *   flags: 当前包附带的状态标志。
 * 输出：
 *   无。
 * 作用：
 *   将当前采样上下文打包成固定 32 字节 USB 二进制帧。
 */
static void app_build_packet(sample_packet_t *pkt, uint16_t flags)
{
  memset(pkt, 0, sizeof(*pkt));
  pkt->magic = SAMPLE_PACKET_MAGIC;       // 固定包头，便于上位机识别帧边界
  pkt->version = SAMPLE_PACKET_VERSION;   // 包格式版本，便于后续升级兼容
  pkt->state = (uint8_t)g_app.state;      // 当前状态机状态，便于上位机了解采样上下文 
  pkt->flags = flags;                     // 当前样本的状态标志，按位定义见 SAMPLE_FLAG_XXX
  pkt->reserved = (uint16_t)(((uint16_t)g_app.last_usb_status << 8) | g_app.last_adc_status); // 低字节记录 ADC 状态，高字节记录 USB 入队结果
  pkt->sequence = g_app.sequence++;       // 样本序列号，单调递增，便于上位机检测丢包和乱序
  pkt->timestamp_us = app_get_timestamp_us(); // 当前时间戳，单位 us
  pkt->raw_code = g_app.raw_code;         // 当前原始 ADC 码值
  pkt->filtered_code = g_app.filtered_code; // 当前滤波结果
  pkt->baseline_code = g_app.baseline_code; // 当前基线值（暗态均值）
  pkt->corrected_code = g_app.corrected_code; // 当前扣除基线后的码值
}

/* 函数说明：
 *   周期性上报故障状态帧。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   在故障态下按固定周期生成状态帧，便于上位机定位问题。
 */
static void app_handle_fault_reporting(void)
{
  sample_packet_t pkt;

  if (!app_has_elapsed(g_app.last_fault_report_ms, APP_FAULT_REPORT_INTERVAL_MS))
  {
    return;
  }

  g_app.last_fault_report_ms = HAL_GetTick();
  app_build_packet(&pkt, (uint16_t)(g_app.fault_flags | SAMPLE_FLAG_FAULT_STATE | SAMPLE_FLAG_FAULT_REPORT));
  g_app.last_usb_status = (uint8_t)usb_stream_enqueue(&pkt);
}

/* 函数说明：
 *   初始化应用层上下文。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   初始化状态机上下文、USB 发送队列和 ADS1220 驱动。
 */
void app_init(void)
{
  memset(&g_app, 0, sizeof(g_app));
  app_enable_cycle_counter();   // 启用 DWT 周期计数器以支持微秒级时间戳
  usb_stream_init();            // 初始化 USB 发送队列
  adc_protocol_init(&hspi1);    // 初始化 ADS1220 驱动，传入 SPI 句柄以供后续通信
  app_set_state(APP_STATE_INIT);// 设置初始状态，等待主循环调度
}

/* 函数说明：
 *   处理 INIT 状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   完成 ADC 复位、默认配置写入和前端偏置启动。
 */
static void app_handle_init_state(void)
{
  adc_protocol_status_t status;

  app_stop_timer();
  HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, GPIO_PIN_SET);

  status = adc_protocol_reset();
  g_app.last_adc_status = (uint8_t)status;
  if (status != ADC_PROTOCOL_OK)
  {
    app_enter_fault(SAMPLE_FLAG_COMM_CHECK_FAILED);
    return;
  }

  status = adc_protocol_configure_default();
  g_app.last_adc_status = (uint8_t)status;
  if (status != ADC_PROTOCOL_OK)
  {
    app_enter_fault(SAMPLE_FLAG_COMM_CHECK_FAILED);
    return;
  }

  if (!app_frontend_bias_start_default())
  {
    app_enter_fault(SAMPLE_FLAG_FAULT_STATE);
    return;
  }

  app_set_state(APP_STATE_BIAS_STABILIZE);
}

/* 函数说明：
 *   处理偏置稳定状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   等待模拟前端稳定后进入通信自检。
 */
static void app_handle_bias_stabilize_state(void)
{
  if (app_has_elapsed(g_app.state_enter_ms, APP_BIAS_STABILIZE_MS))
  {
    app_set_state(APP_STATE_COMM_CHECK);
  }
}

/* 函数说明：
 *   处理通信自检状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   发起一次自检采样，后续通过统一采样链路完成回读校验。
 */
static void app_handle_comm_check_state(void)
{
  app_begin_conversion(APP_PIPELINE_COMM_CHECK);
}

/* 函数说明：
 *   处理暗态校准准备状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   清空校准上下文并启动 TIM6 节拍，准备进入校准闭环。
 */
static void app_handle_dark_calibrate_state(void)
{
  g_app.pipeline_mode = APP_PIPELINE_CALIBRATION;
  g_app.calibration_accumulator = 0;
  g_app.calibration_count = 0U;
  g_app.filter_valid = 0U;
  g_app.baseline_code = 0;
  g_app.corrected_code = 0;
  g_app.fault_flags = 0U;
  app_start_timer();
  app_set_state(APP_STATE_WAIT_TRIGGER);
}

/* 函数说明：
 *   处理等待触发状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   等待 TIM6 节拍，再在主循环中启动一次 ADC 转换。
 */
static void app_handle_wait_trigger_state(void)
{
  if (g_app.evt_sample_tick != 0U)
  {
    g_app.evt_sample_tick = 0U;
    app_begin_conversion(g_app.pipeline_mode);
  }
}

/* 函数说明：
 *   处理等待 DRDY 状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   等待 ADC 数据就绪，若超时则进入故障状态。
 */
static void app_handle_wait_drdy_state(void)
{
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
}

/* 函数说明：
 *   处理样本读取状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   在主循环里完成 SPI 读样，并把协议层状态码回传到应用上下文。
 */
static void app_handle_read_sample_state(void)
{
  adc_protocol_status_t status;

  status = adc_protocol_read_sample(&g_app.raw_code);
  g_app.last_adc_status = (uint8_t)status;
  if (status != ADC_PROTOCOL_OK)
  {
    app_enter_fault(SAMPLE_FLAG_SPI_ERROR |
                    ((g_app.pipeline_mode == APP_PIPELINE_COMM_CHECK) ? SAMPLE_FLAG_COMM_CHECK_FAILED : 0U));
    return;
  }

  app_set_state(APP_STATE_PROCESS_SAMPLE);
}

/* 函数说明：
 *   处理样本加工状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   按当前流水线模式选择自检、校准或正常运行分支。
 */
static void app_handle_process_sample_state(void)
{
  adc_protocol_status_t status;

  (void)app_filter_raw_code(g_app.raw_code);

  if (g_app.pipeline_mode == APP_PIPELINE_COMM_CHECK)
  {
    status = adc_protocol_link_check(g_app.raw_code);
    g_app.last_adc_status = (uint8_t)status;
    if (status != ADC_PROTOCOL_OK)
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
}

/* 函数说明：
 *   处理 USB 刷新状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   打包样本并压入 USB 队列，随后返回等待下一次触发。
 */
static void app_handle_usb_flush_state(void)
{
  sample_packet_t pkt;

  app_build_packet(&pkt, 0U);
  g_app.last_usb_status = (uint8_t)usb_stream_enqueue(&pkt);
  app_set_state(APP_STATE_WAIT_TRIGGER);
}

/* 函数说明：
 *   处理故障状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   周期性发送故障状态帧。
 */
static void app_handle_fault_state(void)
{
  app_handle_fault_reporting();
}

/* 函数说明：
 *   应用层主调度函数。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   推动状态机完成采样、处理和发送流程。
 */
void app_run_once(void)
{
  /* 主循环每次都顺带推动 USB 发送，避免队列长期堆积。 */
  usb_stream_service();

  switch (g_app.state)
  {
    case APP_STATE_INIT:
      app_handle_init_state();
      break;

    case APP_STATE_BIAS_STABILIZE:
      app_handle_bias_stabilize_state();
      break;

    case APP_STATE_COMM_CHECK:
      app_handle_comm_check_state();
      break;

    case APP_STATE_DARK_CALIBRATE:
      app_handle_dark_calibrate_state();
      break;

    case APP_STATE_WAIT_TRIGGER:
      app_handle_wait_trigger_state();
      break;

    case APP_STATE_WAIT_DRDY:
      app_handle_wait_drdy_state();
      break;

    case APP_STATE_READ_SAMPLE:
      app_handle_read_sample_state();
      break;

    case APP_STATE_PROCESS_SAMPLE:
      app_handle_process_sample_state();
      break;

    case APP_STATE_USB_FLUSH:
      app_handle_usb_flush_state();
      break;

    case APP_STATE_FAULT:
      app_handle_fault_state();
      break;

    default:
      app_enter_fault(SAMPLE_FLAG_FAULT_STATE);
      break;
  }

  usb_stream_service();
}

/* 函数说明：
 *   TIM6 中断事件入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   仅置位采样节拍事件。
 */
void app_on_sample_tick_isr(void)
{
  g_app.evt_sample_tick = 1U;
}

/* 函数说明：
 *   DRDY 外部中断事件入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   仅置位数据就绪事件。
 */
void app_on_drdy_isr(void)
{
  g_app.evt_drdy = 1U;
}
