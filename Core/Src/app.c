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
 *    -> APP_STATE_RECOVER
 *    -> 按策略重试或重配；连续失败后进入 APP_STATE_FAULT
 *
 * 模块分工：
 *   main.c         : CubeMX 外设初始化 + 主循环入口
 *   app.c          : 状态机调度、滤波、暗态校准、数据打包
 *   adc_protocol.c : ADS1220 命令、寄存器、读数和码值解析
 *   frame_builder.c: 固定 100 pixels 图像帧构造
 *   usb_stream.c   : USB CDC 图像帧/辅助双队列和重试发送
 *   stm32g4xx_it.c : EXTI0 / TIM6 / USB 中断转发
 * ==========================================================================*/

#include "app.h"

#include <string.h>

#include "adc_protocol.h"
#include "app_config.h"
#include "diag.h"
#include "fault_policy.h"
#include "frame_builder.h"
#include "main.h"
#include "project_config.h"
#include "usb_stream.h"
#include "version.h"

#define APP_SAMPLES_PER_LOGICAL_FRAME (APP_SAMPLE_RATE_HZ / LOGICAL_FRAME_RATE_HZ)
typedef char app_sample_rate_must_divide_frame_rate[(APP_SAMPLE_RATE_HZ % LOGICAL_FRAME_RATE_HZ) == 0U ? 1 : -1];

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

typedef enum
{
  APP_COMMAND_FLAG_NONE = 0U,
  APP_COMMAND_FLAG_INFO = 1U << 0,
  APP_COMMAND_FLAG_PARAMS = 1U << 1
} app_command_flag_t;

typedef enum
{
  APP_META_SUBTYPE_INFO = 0U,
  APP_META_SUBTYPE_PARAMS_TIMING = 1U,
  APP_META_SUBTYPE_PARAMS_ADC = 2U,
  APP_META_SUBTYPE_DIAG_BOOT = 3U
} app_meta_subtype_t;

typedef struct
{
  volatile uint8_t evt_sample_tick; // TIM6 采样节拍事件
  volatile uint8_t evt_drdy;        // ADS1220 DRDY 事件
  volatile uint8_t command_flags;   // USB 命令请求标志，由 CDC 接收回调置位
  app_state_t state;                // 当前状态
  app_pipeline_mode_t pipeline_mode;// 当前样本所属的流水线阶段
  uint32_t state_enter_ms;          // 进入当前状态的时间戳，单位 ms
  uint32_t drdy_deadline_ms;        // 等待 DRDY 的截止时间戳，单位 ms
  uint32_t last_fault_report_ms;    // 上次故障帧发送的时间戳，单位 ms
  uint32_t frame_sequence;          // 10x10 图像帧序列号，仅对正常图像帧递增
  uint32_t frame_sample_count;       // 距离上一图像帧已经处理的运行态样本数
  uint32_t meta_sequence;           // 元信息/故障帧序列号，避免打断样本连续性
  uint32_t calibration_count;       // 已累积的校准样本数量
  int64_t calibration_accumulator;  // 校准样本累加器，用于求暗态基线
  int32_t raw_code;                 // 当前原始 ADC 码值
  int32_t filtered_code;            // 当前滤波结果
  int32_t baseline_code;            // 当前基线值（暗态均值）
  int32_t corrected_code;           // 当前扣除基线后的码值
  uint16_t fault_flags;             // 当前故障标志位，按位定义见 SAMPLE_FLAG_XXX
  uint16_t recovery_fault_flags;     // 触发当前恢复动作的故障标志
  uint8_t filter_valid;             // 滤波器是否已初始化
  uint8_t last_adc_status;          // 最近一次 ADC 协议层状态码
  uint8_t last_usb_status;          // 最近一次 USB 入队结果码
  diag_fault_code_t last_fault_code; // 最近一次细分故障码
  diag_recovery_action_t recovery_action; // 当前待执行恢复动作
  frame_builder_t frame_builder;     // 固定 100 pixels 图像帧构造器
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

static uint16_t app_flags_from_adc_status(adc_protocol_status_t status)
{
  if (status == ADC_PROTOCOL_ERR_TIMEOUT)
  {
    return (uint16_t)(SAMPLE_FLAG_SPI_ERROR | SAMPLE_FLAG_SPI_TIMEOUT);
  }

  if (status == ADC_PROTOCOL_ERR_CONFIG_MISMATCH)
  {
    return SAMPLE_FLAG_CONFIG_MISMATCH;
  }

  return SAMPLE_FLAG_SPI_ERROR;
}

static diag_fault_code_t app_fault_code_from_adc_status(adc_protocol_status_t status)
{
  if (status == ADC_PROTOCOL_ERR_TIMEOUT)
  {
    return DIAG_FAULT_SPI_TIMEOUT;
  }

  if (status == ADC_PROTOCOL_ERR_CONFIG_MISMATCH)
  {
    return DIAG_FAULT_CONFIG_MISMATCH;
  }

  return DIAG_FAULT_SPI_ERROR;
}

static void app_record_usb_enqueue(usb_stream_enqueue_result_t result, bool preserve_fault_root)
{
  g_app.last_usb_status = (uint8_t)result;
  if (result == USB_STREAM_ENQUEUE_OK_DROPPED_OLDEST)
  {
    if (preserve_fault_root != false)
    {
      diag_count_fault(DIAG_FAULT_USB_BUSY_OVERFLOW, g_app.last_usb_status);
    }
    else
    {
      diag_record_fault(DIAG_FAULT_USB_BUSY_OVERFLOW, g_app.last_usb_status);
    }
  }
}

static void app_enter_fault_hold(uint16_t reason_flags)
{
  g_app.fault_flags |= reason_flags;
  app_stop_timer();
  (void)adc_protocol_stop();
  g_app.evt_sample_tick = 0U;
  g_app.evt_drdy = 0U;
  g_app.last_fault_report_ms = HAL_GetTick() - APP_FAULT_REPORT_INTERVAL_MS;
  app_set_state(APP_STATE_FAULT);
}

static void app_schedule_recovery(diag_fault_code_t code, uint16_t reason_flags)
{
  fault_policy_decision_t decision;

  g_app.last_fault_code = code;
  g_app.fault_flags |= reason_flags;
  diag_record_fault(code, g_app.last_adc_status);
  decision = fault_policy_on_fault(code);

  if (decision.action == DIAG_RECOVERY_ACTION_NONE)
  {
    return;
  }

  if (decision.enter_fault_hold != 0U)
  {
    if (code != DIAG_FAULT_INIT_FAILED)
    {
      diag_record_fault(DIAG_FAULT_RECOVERY_FAILED, g_app.last_adc_status);
    }
    diag_record_recovery(DIAG_RECOVERY_ACTION_FAULT_HOLD, DIAG_RECOVERY_RESULT_FAILED);
    app_enter_fault_hold((uint16_t)(reason_flags | SAMPLE_FLAG_RECOVERY_FAILED));
    return;
  }

  g_app.recovery_action = decision.action;
  g_app.recovery_fault_flags = reason_flags;
  app_stop_timer();
  (void)adc_protocol_stop();
  g_app.evt_sample_tick = 0U;
  g_app.evt_drdy = 0U;
  app_set_state(APP_STATE_RECOVER);
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
    app_schedule_recovery(app_fault_code_from_adc_status(status),
                          (uint16_t)(app_flags_from_adc_status(status) |
                                     ((pipeline_mode == APP_PIPELINE_COMM_CHECK) ? SAMPLE_FLAG_COMM_CHECK_FAILED : 0U)));
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
 *   初始化一个固定 32 字节辅助 USB 帧头。
 * 输入：
 *   pkt: 输出数据包指针。
 *   flags: 当前包附带的状态标志。
 *   reserved: 元信息帧时保存子类型，故障帧时保存链路状态。
 *   sequence: 当前帧使用的序列号。
 * 输出：
 *   无。
 * 作用：
 *   统一填充辅助包头字段，故障/元信息帧共享这一步。
 */
static void app_prepare_packet(sample_packet_t *pkt, uint16_t flags, uint16_t reserved, uint32_t sequence)
{
  memset(pkt, 0, sizeof(*pkt));
  pkt->magic = SAMPLE_PACKET_MAGIC;       // 固定包头，便于上位机识别帧边界
  pkt->version = SAMPLE_PACKET_VERSION;   // 包格式版本，便于后续升级兼容
  pkt->state = (uint8_t)g_app.state;      // 当前状态机状态，便于上位机了解采样上下文 
  pkt->flags = flags;                     // 当前样本的状态标志，按位定义见 SAMPLE_FLAG_XXX
  pkt->reserved = reserved;
  pkt->sequence = sequence;
  pkt->timestamp_us = app_get_timestamp_us(); // 当前时间戳，单位 us
}

/* 函数说明：
 *   构造一个普通样本数据包。
 * 输入：
 *   pkt: 输出数据包指针。
 *   flags: 当前包附带的状态标志。
 * 输出：
 *   无。
 * 作用：
 *   将当前采样上下文打包成固定 32 字节 USB 样本帧。
 */
/* 函数说明：
 *   构造一个元信息帧。
 * 输入：
 *   pkt: 输出数据包指针。
 *   flags: 当前包附带的元信息标志。
 *   subtype: 元信息子类型编号。
 *   payload0~3: 4 个 32 bit 元信息负载。
 * 输出：
 *   无。
 * 作用：
 *   在不改变固定样本格式长度的前提下复用同一帧结构发送版本和参数。
 */
static void app_build_meta_packet(sample_packet_t *pkt,
                                  uint16_t flags,
                                  uint16_t subtype,
                                  int32_t payload0,
                                  int32_t payload1,
                                  int32_t payload2,
                                  int32_t payload3)
{
  app_prepare_packet(pkt, flags, subtype, g_app.meta_sequence++);
  pkt->raw_code = payload0;
  pkt->filtered_code = payload1;
  pkt->baseline_code = payload2;
  pkt->corrected_code = payload3;
}

/* 函数说明：
 *   构造一个故障状态帧。
 * 输入：
 *   pkt: 输出数据包指针。
 *   flags: 当前故障帧标志。
 * 输出：
 *   无。
 * 作用：
 *   故障态沿用 32 字节辅助负载布局回传最近一次测量上下文。
 */
static void app_build_fault_packet(sample_packet_t *pkt, uint16_t flags)
{
  diag_snapshot_t diag_snapshot;
  fault_policy_snapshot_t policy_snapshot;

  diag_get_snapshot(&diag_snapshot);
  fault_policy_get_snapshot(&policy_snapshot);

  app_prepare_packet(pkt,
                     flags,
                     (uint16_t)(((uint16_t)g_app.last_usb_status << 8) | diag_snapshot.last_protocol_status),
                     g_app.meta_sequence++);
  pkt->raw_code = (int32_t)diag_snapshot.last_fault;
  pkt->filtered_code = (int32_t)diag_get_fault_count(diag_snapshot.last_fault);
  pkt->baseline_code = (int32_t)(((uint32_t)diag_snapshot.reset_reason << 16) |
                                 ((uint32_t)diag_snapshot.last_recovery_action << 8) |
                                 (uint32_t)diag_snapshot.last_recovery_result);
  pkt->corrected_code = (int32_t)(((policy_snapshot.consecutive_failures & 0xFFFFUL) << 16) |
                                  (policy_snapshot.recovery_attempts & 0xFFFFUL));
}

/* 函数说明：
 *   发送一组基线版本与参数帧。
 * 输入：
 *   send_info: 是否发送版本/签名信息帧。
 *   send_params: 是否发送参数冻结信息帧。
 * 输出：
 *   无。
 * 作用：
 *   支持上电自动上报，也支持上位机通过命令重发基线描述。
 */
static void app_send_baseline_metadata(uint8_t send_info, uint8_t send_params)
{
  sample_packet_t pkt;
  version_descriptor_t descriptor;
  diag_snapshot_t diag_snapshot;

  version_get_descriptor(&descriptor);

  if (send_info != 0U)
  {
    app_build_meta_packet(&pkt,
                          SAMPLE_FLAG_INFO_FRAME,
                          APP_META_SUBTYPE_INFO,
                          (int32_t)descriptor.semver_packed,
                          (int32_t)descriptor.build_number,
                          (int32_t)descriptor.packet_version,
                          (int32_t)descriptor.param_signature);
    app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt), true);
  }

  if (send_params != 0U)
  {
    app_build_meta_packet(&pkt,
                          SAMPLE_FLAG_PARAM_FRAME,
                          APP_META_SUBTYPE_PARAMS_TIMING,
                          (int32_t)APP_SAMPLE_RATE_HZ,
                          (int32_t)APP_BIAS_STABILIZE_MS,
                          (int32_t)APP_DARK_CALIBRATION_SAMPLES,
                          (int32_t)APP_DRDY_TIMEOUT_MS);
    app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt), true);

    app_build_meta_packet(&pkt,
                          SAMPLE_FLAG_PARAM_FRAME,
                          APP_META_SUBTYPE_PARAMS_ADC,
                          (int32_t)APP_FILTER_ALPHA_SHIFT,
                          (int32_t)APP_USB_QUEUE_DEPTH,
                          (int32_t)(((uint32_t)APP_DAC_BIAS_CH2 << 16) | (uint32_t)APP_DAC_BIAS_CH1),
                          (int32_t)descriptor.ads1220_default_config);
    app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt), true);

    diag_get_snapshot(&diag_snapshot);
    app_build_meta_packet(&pkt,
                          (uint16_t)(SAMPLE_FLAG_DIAG_FRAME | SAMPLE_FLAG_PARAM_FRAME),
                          APP_META_SUBTYPE_DIAG_BOOT,
                          (int32_t)diag_snapshot.reset_reason,
                          (int32_t)diag_snapshot.last_fault,
                          (int32_t)diag_snapshot.total_faults,
                          (int32_t)(((uint32_t)diag_snapshot.last_recovery_action << 16) |
                                    (uint32_t)diag_snapshot.last_recovery_result));
    app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt), true);
  }
}

/* 函数说明：
 *   处理 CDC 接收回调置位的最小命令。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   把异步命令请求收口到主循环里执行，避免在 USB 回调上下文里直接入队数据。
 */
static void app_process_pending_commands(void)
{
  uint32_t primask;
  uint8_t command_flags;

  primask = __get_PRIMASK();  // 先保存当前中断状态，再清空命令标志以避免重复处理，最后根据之前的中断状态决定是否恢复中断。
  __disable_irq();            // 进入临界区保护命令标志，避免与 USB 中断回调冲突
  command_flags = g_app.command_flags;
  g_app.command_flags = APP_COMMAND_FLAG_NONE;  // 清空命令标志，准备接受下一轮命令请求
  if (primask == 0U)
  {
    __enable_irq();
  }

  if (command_flags == APP_COMMAND_FLAG_NONE)
  {
    return;
  }

  app_send_baseline_metadata((uint8_t)(command_flags & APP_COMMAND_FLAG_INFO),
                             (uint8_t)(command_flags & APP_COMMAND_FLAG_PARAMS));
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
  app_build_fault_packet(&pkt, (uint16_t)(g_app.fault_flags | SAMPLE_FLAG_FAULT_STATE | SAMPLE_FLAG_FAULT_REPORT));
  app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt), true);
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
  diag_init();                    // 捕获本次启动的复位原因并清空诊断计数
  fault_policy_init();            // 清空恢复策略的连续失败状态
  frame_builder_init(&g_app.frame_builder, FRAME_TYPE_PARTIAL_REAL);
  app_enable_cycle_counter();   // 启用 DWT 周期计数器以支持微秒级时间戳
  usb_stream_init();            // 初始化 USB 发送队列
  adc_protocol_init(&hspi1);    // 初始化 ADS1220 驱动，传入 SPI 句柄以供后续通信
  g_app.command_flags = (uint8_t)(APP_COMMAND_FLAG_INFO | APP_COMMAND_FLAG_PARAMS);
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
    app_schedule_recovery(app_fault_code_from_adc_status(status),
                          (uint16_t)(app_flags_from_adc_status(status) | SAMPLE_FLAG_COMM_CHECK_FAILED));
    return;
  }

  status = adc_protocol_configure_default();
  g_app.last_adc_status = (uint8_t)status;
  if (status != ADC_PROTOCOL_OK)
  {
    app_schedule_recovery(app_fault_code_from_adc_status(status),
                          (uint16_t)(app_flags_from_adc_status(status) | SAMPLE_FLAG_COMM_CHECK_FAILED));
    return;
  }

  if (!app_frontend_bias_start_default())
  {
    g_app.last_adc_status = ADC_PROTOCOL_OK;
    app_schedule_recovery(DIAG_FAULT_INIT_FAILED, SAMPLE_FLAG_FAULT_STATE);
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
  g_app.frame_sample_count = 0U;
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
    app_schedule_recovery(DIAG_FAULT_DRDY_TIMEOUT,
                          (uint16_t)(SAMPLE_FLAG_DRDY_TIMEOUT |
                                     ((g_app.pipeline_mode == APP_PIPELINE_COMM_CHECK) ? SAMPLE_FLAG_COMM_CHECK_FAILED : 0U)));
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
    app_schedule_recovery(app_fault_code_from_adc_status(status),
                          (uint16_t)(app_flags_from_adc_status(status) |
                                     ((g_app.pipeline_mode == APP_PIPELINE_COMM_CHECK) ? SAMPLE_FLAG_COMM_CHECK_FAILED : 0U)));
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
      app_schedule_recovery(app_fault_code_from_adc_status(status),
                            (uint16_t)(app_flags_from_adc_status(status) | SAMPLE_FLAG_COMM_CHECK_FAILED));
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
  frame_packet_t frame;

  /* ADS1220 当前仍按 1 kHz 单通道采样，V1.0 对外只发布 100 Hz 逻辑图像帧。 */
  g_app.frame_sample_count++;
  if (g_app.frame_sample_count < APP_SAMPLES_PER_LOGICAL_FRAME)
  {
    app_set_state(APP_STATE_WAIT_TRIGGER);
    return;
  }

  g_app.frame_sample_count = 0U;
  frame_builder_build(&g_app.frame_builder,
                      &frame,
                      g_app.frame_sequence++,
                      app_get_timestamp_us(),
                      g_app.corrected_code);
  app_record_usb_enqueue(usb_stream_enqueue_frame(&frame), false);
  app_set_state(APP_STATE_WAIT_TRIGGER);
}

static void app_finish_recovery(diag_recovery_action_t action, bool success)
{
  diag_record_recovery(action, (success != false) ? DIAG_RECOVERY_RESULT_SUCCESS : DIAG_RECOVERY_RESULT_FAILED);
  fault_policy_record_recovery_result(action, success);
}

static void app_handle_recover_state(void)
{
  adc_protocol_status_t status;
  diag_recovery_action_t action = g_app.recovery_action;

  if (action == DIAG_RECOVERY_ACTION_SPI_RETRY)
  {
    g_app.evt_drdy = 0U;
    status = adc_protocol_start_conversion();
    g_app.last_adc_status = (uint8_t)status;
    if (status != ADC_PROTOCOL_OK)
    {
      app_finish_recovery(action, false);
      app_schedule_recovery(app_fault_code_from_adc_status(status),
                            (uint16_t)(app_flags_from_adc_status(status) |
                                       g_app.recovery_fault_flags |
                                       SAMPLE_FLAG_RECOVERY_ACTIVE));
      return;
    }

    g_app.fault_flags = 0U;
    g_app.recovery_fault_flags = 0U;
    g_app.recovery_action = DIAG_RECOVERY_ACTION_NONE;
    app_finish_recovery(action, true);
    g_app.drdy_deadline_ms = HAL_GetTick() + APP_DRDY_TIMEOUT_MS;
    app_set_state(APP_STATE_WAIT_DRDY);
    return;
  }

  if (action == DIAG_RECOVERY_ACTION_ADC_RECONFIGURE)
  {
    status = adc_protocol_reset();
    if (status == ADC_PROTOCOL_OK)
    {
      status = adc_protocol_configure_default();
    }
    if (status == ADC_PROTOCOL_OK)
    {
      status = adc_protocol_link_check(g_app.raw_code);
    }

    g_app.last_adc_status = (uint8_t)status;
    if (status != ADC_PROTOCOL_OK)
    {
      app_finish_recovery(action, false);
      app_schedule_recovery(app_fault_code_from_adc_status(status),
                            (uint16_t)(app_flags_from_adc_status(status) |
                                       g_app.recovery_fault_flags |
                                       SAMPLE_FLAG_RECOVERY_ACTIVE |
                                       SAMPLE_FLAG_RECOVERY_FAILED));
      return;
    }

    g_app.fault_flags = 0U;
    g_app.recovery_fault_flags = 0U;
    g_app.recovery_action = DIAG_RECOVERY_ACTION_NONE;
    g_app.pipeline_mode = APP_PIPELINE_COMM_CHECK;
    g_app.filter_valid = 0U;
    app_finish_recovery(action, true);
    app_set_state(APP_STATE_BIAS_STABILIZE);
    return;
  }

  app_finish_recovery(action, false);
  app_enter_fault_hold((uint16_t)(g_app.fault_flags | SAMPLE_FLAG_RECOVERY_FAILED));
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
  app_process_pending_commands();

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

    case APP_STATE_RECOVER:
      app_handle_recover_state();
      break;

    case APP_STATE_FAULT:
      app_handle_fault_state();
      break;

    default:
      app_enter_fault_hold(SAMPLE_FLAG_FAULT_STATE);
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

/* 函数说明：
 *   处理上位机发来的最小命令字节。
 * 输入：
 *   data: 接收缓冲区。
 *   length: 缓冲区长度。
 * 输出：
 *   无。
 * 作用：
 *   识别 I/P/B 查询命令，主循环会据此补发版本和参数元信息帧。
 */
void app_on_usb_command_rx(const uint8_t *data, uint32_t length)
{
  uint32_t i;

  if ((data == NULL) || (length == 0U))
  {
    return;
  }

  for (i = 0U; i < length; ++i)
  {
    switch (data[i])
    {
      case APP_USB_COMMAND_INFO:
      case 'i':
        g_app.command_flags |= APP_COMMAND_FLAG_INFO;
        break;

      case APP_USB_COMMAND_PARAMS:
      case 'p':
        g_app.command_flags |= APP_COMMAND_FLAG_PARAMS;
        break;

      case APP_USB_COMMAND_BASELINE:
      case 'b':
        g_app.command_flags |= (uint8_t)(APP_COMMAND_FLAG_INFO | APP_COMMAND_FLAG_PARAMS);
        break;

      default:
        break;
    }
  }
}

#ifdef UNIT_TEST
/* 函数说明：
 *   获取当前应用状态机状态。
 * 输入：
 *   无。
 * 输出：
 *   返回当前 app_state_t 状态值。
 * 作用：
 *   仅在 UNIT_TEST 构建下提供只读观察口，便于主机测试断言状态迁移。
 */
app_state_t app_test_get_state(void)
{
  return g_app.state;
}

/* 函数说明：
 *   获取当前故障标志。
 * 输入：
 *   无。
 * 输出：
 *   返回当前 fault_flags 位图。
 * 作用：
 *   仅在 UNIT_TEST 构建下暴露故障状态，避免测试直接访问静态上下文。
 */
uint16_t app_test_get_fault_flags(void)
{
  return g_app.fault_flags;
}

/* 函数说明：
 *   获取当前暗态基线码值。
 * 输入：
 *   无。
 * 输出：
 *   返回当前 baseline_code。
 * 作用：
 *   仅在 UNIT_TEST 构建下验证校准结果，不提供任何写入口。
 */
int32_t app_test_get_baseline_code(void)
{
  return g_app.baseline_code;
}
#endif
