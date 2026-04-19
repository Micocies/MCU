#include "app.h"

#include <string.h>

#include "adc_bus.h"
#include "adc_protocol.h"
#include "ads1220_device.h"
#include "ads1220_scheduler.h"
#include "app_config.h"
#include "diag.h"
#include "fault_policy.h"
#include "frame_builder.h"
#include "project_config.h"
#include "usb_stream.h"
#include "version.h"

extern DAC_HandleTypeDef hdac1;
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim6;

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
  volatile uint32_t pending_output_ticks;
  volatile uint32_t pending_drdy_count;
  volatile uint8_t command_flags;
  app_state_t state;
  uint32_t state_enter_ms;
  uint32_t last_fault_report_ms;
  uint32_t frame_sequence;
  uint32_t meta_sequence;
  uint16_t fault_flags;
  uint16_t recovery_fault_flags;
  uint8_t last_adc_status;
  uint8_t last_usb_status;
  uint8_t pending_drdy_device_id;
  uint8_t pending_drdy_channel_id;
  diag_fault_code_t last_fault_code;
  diag_recovery_action_t recovery_action;
  adc_bus_t adc_bus;
  ads1220_scheduler_t scheduler;
  frame_builder_t frame_builder;
  int32_t pixels[PIXEL_COUNT];
} app_context_t;

static app_context_t g_app;

static void app_set_state(app_state_t state)
{
  g_app.state = state;
  g_app.state_enter_ms = HAL_GetTick();
}

static bool app_has_elapsed(uint32_t start_ms, uint32_t duration_ms)
{
  return ((HAL_GetTick() - start_ms) >= duration_ms);
}

static void app_clear_pending_events(void)
{
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  g_app.pending_output_ticks = 0U;
  g_app.pending_drdy_count = 0U;
  g_app.pending_drdy_device_id = 0xFFU;
  g_app.pending_drdy_channel_id = 0xFFU;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void app_clear_pending_drdy_events(void)
{
  uint32_t primask;

  primask = __get_PRIMASK();
  __disable_irq();
  g_app.pending_drdy_count = 0U;
  g_app.pending_drdy_device_id = 0xFFU;
  g_app.pending_drdy_channel_id = 0xFFU;
#ifndef UNIT_TEST
  __HAL_GPIO_EXTI_CLEAR_IT(ADC_DRDY_MUX_Pin);
#endif
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static bool app_take_selected_drdy_event(void)
{
  uint32_t primask;
  bool pending = false;
  uint8_t device_id;
  uint8_t channel_id;

  device_id = ads1220_scheduler_current_device_id(&g_app.scheduler);
  channel_id = ads1220_scheduler_current_channel_id(&g_app.scheduler);

  primask = __get_PRIMASK();
  __disable_irq();
  if ((g_app.pending_drdy_count != 0U) &&
      (g_app.pending_drdy_device_id == device_id) &&
      (g_app.pending_drdy_channel_id == channel_id))
  {
    g_app.pending_drdy_count--;
    pending = true;
    if (g_app.pending_drdy_count == 0U)
    {
      g_app.pending_drdy_device_id = 0xFFU;
      g_app.pending_drdy_channel_id = 0xFFU;
    }
  }
  if (primask == 0U)
  {
    __enable_irq();
  }

  return pending;
}

static bool app_consume_output_tick(void)
{
  uint32_t primask;
  bool consumed = false;

  primask = __get_PRIMASK();
  __disable_irq();
  if (g_app.pending_output_ticks != 0U)
  {
    g_app.pending_output_ticks--;
    consumed = true;
  }
  if (primask == 0U)
  {
    __enable_irq();
  }

  return consumed;
}

static uint32_t app_get_timestamp_us(void)
{
  if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U)
  {
    return HAL_GetTick() * 1000U;
  }

  return (uint32_t)(DWT->CYCCNT / (SystemCoreClock / 1000000U));
}

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

static void app_prepare_packet(sample_packet_t *pkt, uint16_t flags, uint16_t reserved, uint32_t sequence)
{
  memset(pkt, 0, sizeof(*pkt));
  pkt->magic = SAMPLE_PACKET_MAGIC;
  pkt->version = SAMPLE_PACKET_VERSION;
  pkt->state = (uint8_t)g_app.state;
  pkt->flags = flags;
  pkt->reserved = reserved;
  pkt->sequence = sequence;
  pkt->timestamp_us = app_get_timestamp_us();
}

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

static void app_record_usb_enqueue(usb_stream_enqueue_result_t result)
{
  g_app.last_usb_status = (uint8_t)result;
  if (result == USB_STREAM_ENQUEUE_OK_DROPPED_OLDEST)
  {
    g_app.fault_flags |= SAMPLE_FLAG_USB_OVERFLOW;
  }
}

static uint16_t app_flags_from_adc_status(adc_protocol_status_t status,
                                          ads1220_scheduler_state_t error_state)
{
  if ((status == ADC_PROTOCOL_ERR_TIMEOUT) && (error_state == ADS1220_SCHED_WAIT_DRDY))
  {
    return SAMPLE_FLAG_DRDY_TIMEOUT;
  }

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

static diag_fault_code_t app_fault_code_from_adc_status(adc_protocol_status_t status,
                                                        ads1220_scheduler_state_t error_state)
{
  if ((status == ADC_PROTOCOL_ERR_TIMEOUT) && (error_state == ADS1220_SCHED_WAIT_DRDY))
  {
    return DIAG_FAULT_DRDY_TIMEOUT;
  }

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

static void app_enter_fault_hold(uint16_t reason_flags)
{
  g_app.fault_flags |= reason_flags;
  app_stop_timer();
  ads1220_scheduler_stop(&g_app.scheduler);
  adc_bus_cs_deassert();
  app_clear_pending_events();
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
  ads1220_scheduler_stop(&g_app.scheduler);
  app_clear_pending_events();
  app_set_state(APP_STATE_RECOVER);
}

static void app_finish_recovery(diag_recovery_action_t action, bool success)
{
  diag_record_recovery(action, (success != false) ? DIAG_RECOVERY_RESULT_SUCCESS : DIAG_RECOVERY_RESULT_FAILED);
  fault_policy_record_recovery_result(action, success);
}

static adc_protocol_status_t app_reconfigure_current_device(void)
{
  ads1220_device_t *dev;
  uint8_t device_id;
  uint8_t channel_id;
  adc_protocol_status_t status;

  device_id = ads1220_scheduler_current_device_id(&g_app.scheduler);
  channel_id = ads1220_scheduler_current_channel_id(&g_app.scheduler);

  adc_bus_reset_all();
  adc_bus_start_all_pulse();
  ads1220_device_table_init();

  dev = ads1220_device_get(device_id);
  if (dev == 0)
  {
    return ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  status = ads1220_device_configure_channel(dev, channel_id);
  if (status == ADC_PROTOCOL_OK)
  {
    status = ads1220_device_link_check(dev);
  }

  return status;
}

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
    app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt));
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
    app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt));

    app_build_meta_packet(&pkt,
                          SAMPLE_FLAG_PARAM_FRAME,
                          APP_META_SUBTYPE_PARAMS_ADC,
                          (int32_t)APP_FILTER_ALPHA_SHIFT,
                          (int32_t)ADC_DEVICE_COUNT,
                          (int32_t)ADC_CHANNELS_PER_DEVICE,
                          (int32_t)descriptor.ads1220_default_config);
    app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt));

    diag_get_snapshot(&diag_snapshot);
    app_build_meta_packet(&pkt,
                          (uint16_t)(SAMPLE_FLAG_DIAG_FRAME | SAMPLE_FLAG_PARAM_FRAME),
                          APP_META_SUBTYPE_DIAG_BOOT,
                          (int32_t)diag_snapshot.reset_reason,
                          (int32_t)diag_snapshot.last_fault,
                          (int32_t)diag_snapshot.total_faults,
                          (int32_t)(((uint32_t)diag_snapshot.last_recovery_action << 16) |
                                    (uint32_t)diag_snapshot.last_recovery_result));
    app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt));
  }
}

static void app_process_pending_commands(void)
{
  uint32_t primask;
  uint8_t command_flags;

  primask = __get_PRIMASK();
  __disable_irq();
  command_flags = g_app.command_flags;
  g_app.command_flags = APP_COMMAND_FLAG_NONE;
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

static void app_handle_init_state(void)
{
  app_stop_timer();
  app_clear_pending_events();
  memset(g_app.pixels, 0, sizeof(g_app.pixels));
  adc_bus_reset_all();
  adc_bus_start_all_pulse();
  ads1220_device_table_init();
  ads1220_scheduler_start(&g_app.scheduler);

  if (!app_frontend_bias_start_default())
  {
    g_app.last_adc_status = ADC_PROTOCOL_OK;
    app_schedule_recovery(DIAG_FAULT_INIT_FAILED, SAMPLE_FLAG_FAULT_STATE);
    return;
  }

  app_set_state(APP_STATE_BIAS_STABILIZE);
}

static void app_handle_bias_stabilize_state(void)
{
  if (!app_has_elapsed(g_app.state_enter_ms, APP_BIAS_STABILIZE_MS))
  {
    return;
  }

  app_clear_pending_events();
  app_start_timer();
  app_set_state(APP_STATE_WAIT_DRDY);
}

static void app_handle_scheduler_state(void)
{
  bool selected_drdy_pending = false;
  adc_protocol_status_t status;
  ads1220_scheduler_state_t error_state;
  diag_fault_code_t fault_code;
  uint16_t fault_flags;

  if (g_app.scheduler.state == ADS1220_SCHED_WAIT_DRDY)
  {
    selected_drdy_pending = app_take_selected_drdy_event();
  }

  ads1220_scheduler_service(&g_app.scheduler, selected_drdy_pending);
  if (ads1220_scheduler_consume_drdy_rearm_request(&g_app.scheduler))
  {
    app_clear_pending_drdy_events();
  }

  g_app.last_adc_status = (uint8_t)g_app.scheduler.last_status;

  if (ads1220_scheduler_has_error(&g_app.scheduler))
  {
    status = g_app.scheduler.last_status;
    error_state = ads1220_scheduler_error_state(&g_app.scheduler);
    fault_code = app_fault_code_from_adc_status(status, error_state);
    fault_flags = app_flags_from_adc_status(status, error_state);
    app_schedule_recovery(fault_code, fault_flags);
    return;
  }

  if (ads1220_scheduler_frame_ready(&g_app.scheduler) && app_consume_output_tick())
  {
    app_set_state(APP_STATE_USB_FLUSH);
  }
}

static void app_handle_usb_flush_state(void)
{
  frame_packet_t frame;

  frame_builder_build_pixels(&g_app.frame_builder,
                             &frame,
                             g_app.frame_sequence++,
                             app_get_timestamp_us(),
                             g_app.pixels);
  app_record_usb_enqueue(usb_stream_enqueue_frame(&frame));
  ads1220_scheduler_clear_frame_ready(&g_app.scheduler);
  app_set_state(APP_STATE_WAIT_DRDY);
}

static void app_handle_fault_state(void)
{
  sample_packet_t pkt;
  diag_snapshot_t diag_snapshot;
  fault_policy_snapshot_t policy_snapshot;

  if (!app_has_elapsed(g_app.last_fault_report_ms, APP_FAULT_REPORT_INTERVAL_MS))
  {
    return;
  }

  g_app.last_fault_report_ms = HAL_GetTick();
  diag_get_snapshot(&diag_snapshot);
  fault_policy_get_snapshot(&policy_snapshot);

  app_prepare_packet(&pkt,
                     (uint16_t)(g_app.fault_flags | SAMPLE_FLAG_FAULT_STATE | SAMPLE_FLAG_FAULT_REPORT),
                     (uint16_t)(((uint16_t)g_app.last_usb_status << 8) | diag_snapshot.last_protocol_status),
                     g_app.meta_sequence++);
  pkt.raw_code = (int32_t)diag_snapshot.last_fault;
  pkt.filtered_code = (int32_t)diag_get_fault_count(diag_snapshot.last_fault);
  pkt.baseline_code = (int32_t)(((uint32_t)diag_snapshot.reset_reason << 16) |
                                ((uint32_t)diag_snapshot.last_recovery_action << 8) |
                                (uint32_t)diag_snapshot.last_recovery_result);
  pkt.corrected_code = (int32_t)(((policy_snapshot.consecutive_failures & 0xFFFFUL) << 16) |
                                 (policy_snapshot.recovery_attempts & 0xFFFFUL));
  app_record_usb_enqueue(usb_stream_enqueue_aux(&pkt));
}

static void app_handle_recover_state(void)
{
  adc_protocol_status_t status;
  diag_recovery_action_t action = g_app.recovery_action;

  if (action == DIAG_RECOVERY_ACTION_SPI_RETRY)
  {
    ads1220_scheduler_retry_current(&g_app.scheduler);
    app_clear_pending_events();
    app_start_timer();
    g_app.fault_flags = 0U;
    g_app.recovery_fault_flags = 0U;
    g_app.recovery_action = DIAG_RECOVERY_ACTION_NONE;
    app_finish_recovery(action, true);
    app_set_state(APP_STATE_WAIT_DRDY);
    return;
  }

  if (action == DIAG_RECOVERY_ACTION_ADC_RECONFIGURE)
  {
    status = app_reconfigure_current_device();
    g_app.last_adc_status = (uint8_t)status;
    if (status != ADC_PROTOCOL_OK)
    {
      app_finish_recovery(action, false);
      app_schedule_recovery(app_fault_code_from_adc_status(status, ADS1220_SCHED_ENSURE_CONFIG),
                            (uint16_t)(app_flags_from_adc_status(status, ADS1220_SCHED_ENSURE_CONFIG) |
                                       g_app.recovery_fault_flags |
                                       SAMPLE_FLAG_RECOVERY_ACTIVE |
                                       SAMPLE_FLAG_RECOVERY_FAILED));
      return;
    }

    ads1220_scheduler_retry_current(&g_app.scheduler);
    app_clear_pending_events();
    app_start_timer();
    g_app.fault_flags = 0U;
    g_app.recovery_fault_flags = 0U;
    g_app.recovery_action = DIAG_RECOVERY_ACTION_NONE;
    app_finish_recovery(action, true);
    app_set_state(APP_STATE_WAIT_DRDY);
    return;
  }

  app_finish_recovery(action, false);
  app_enter_fault_hold((uint16_t)(g_app.fault_flags | SAMPLE_FLAG_RECOVERY_FAILED));
}

void app_init(void)
{
  memset(&g_app, 0, sizeof(g_app));
  diag_init();
  fault_policy_init();
  frame_builder_init(&g_app.frame_builder, FRAME_TYPE_FULL_REAL);
  app_enable_cycle_counter();
  usb_stream_init();
  adc_bus_init(&g_app.adc_bus, &hspi1);
  adc_protocol_init(&g_app.adc_bus);
  ads1220_device_table_init();
  ads1220_scheduler_init(&g_app.scheduler, g_app.pixels);
  g_app.pending_drdy_device_id = 0xFFU;
  g_app.pending_drdy_channel_id = 0xFFU;
  g_app.command_flags = (uint8_t)(APP_COMMAND_FLAG_INFO | APP_COMMAND_FLAG_PARAMS);
  app_set_state(APP_STATE_INIT);
}

void app_run_once(void)
{
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

    case APP_STATE_WAIT_DRDY:
    case APP_STATE_WAIT_TRIGGER:
    case APP_STATE_READ_SAMPLE:
    case APP_STATE_PROCESS_SAMPLE:
      app_handle_scheduler_state();
      break;

    case APP_STATE_USB_FLUSH:
      app_handle_usb_flush_state();
      break;

    case APP_STATE_FAULT:
      app_handle_fault_state();
      break;

    case APP_STATE_RECOVER:
      app_handle_recover_state();
      break;

    case APP_STATE_COMM_CHECK:
    case APP_STATE_DARK_CALIBRATE:
    default:
      app_set_state(APP_STATE_WAIT_DRDY);
      break;
  }

  usb_stream_service();
}

void app_on_sample_tick_isr(void)
{
  if (g_app.pending_output_ticks != UINT32_MAX)
  {
    g_app.pending_output_ticks++;
  }
}

void app_on_drdy_isr(void)
{
  uint8_t device_id;
  uint8_t channel_id;

  if ((g_app.state != APP_STATE_WAIT_DRDY) || (g_app.scheduler.state != ADS1220_SCHED_WAIT_DRDY))
  {
    return;
  }

  device_id = ads1220_scheduler_current_device_id(&g_app.scheduler);
  channel_id = ads1220_scheduler_current_channel_id(&g_app.scheduler);
  if ((g_app.pending_drdy_count != 0U) &&
      ((g_app.pending_drdy_device_id != device_id) ||
       (g_app.pending_drdy_channel_id != channel_id)))
  {
    g_app.pending_drdy_count = 0U;
  }

  g_app.pending_drdy_device_id = device_id;
  g_app.pending_drdy_channel_id = channel_id;
  if (g_app.pending_drdy_count != UINT32_MAX)
  {
    g_app.pending_drdy_count++;
  }
}

void app_on_usb_command_rx(const uint8_t *data, uint32_t length)
{
  uint32_t i;

  if ((data == 0) || (length == 0U))
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
app_state_t app_test_get_state(void)
{
  return g_app.state;
}

uint16_t app_test_get_fault_flags(void)
{
  return g_app.fault_flags;
}

int32_t app_test_get_baseline_code(void)
{
  return 0;
}
#endif
