#include "ads1220_scheduler.h"

#include <string.h>

#include "adc_bus.h"
#include "ads1220_device.h"
#include "app_config.h"
#include "board_topology.h"

static void ads1220_scheduler_advance_indices(ads1220_scheduler_t *scheduler)
{
  scheduler->current_channel_id++;
  if (scheduler->current_channel_id >= ADC_CHANNELS_PER_DEVICE)
  {
    scheduler->current_channel_id = 0U;
    scheduler->current_device_id++;
    if (scheduler->current_device_id >= ADC_DEVICE_COUNT)
    {
      scheduler->current_device_id = 0U;
      scheduler->frame_ready = 1U;
    }
  }
}

static void ads1220_scheduler_raise_error(ads1220_scheduler_t *scheduler,
                                          adc_protocol_status_t status,
                                          ads1220_scheduler_state_t state)
{
  scheduler->last_status = status;
  scheduler->error_state = state;
  scheduler->error_pending = 1U;
  scheduler->running = 0U;
  adc_bus_cs_deassert();
}

void ads1220_scheduler_init(ads1220_scheduler_t *scheduler, int32_t pixels[PIXEL_COUNT])
{
  if (scheduler == 0)
  {
    return;
  }

  memset(scheduler, 0, sizeof(*scheduler));
  scheduler->pixels = pixels;
  scheduler->state = ADS1220_SCHED_SELECT_DEVICE;
  scheduler->last_status = ADC_PROTOCOL_OK;
}

void ads1220_scheduler_start(ads1220_scheduler_t *scheduler)
{
  if (scheduler == 0)
  {
    return;
  }

  scheduler->current_device_id = 0U;
  scheduler->current_channel_id = 0U;
  scheduler->frame_ready = 0U;
  scheduler->running = 1U;
  scheduler->error_pending = 0U;
  scheduler->drdy_rearm_pending = 0U;
  scheduler->state = ADS1220_SCHED_SELECT_DEVICE;
  scheduler->last_status = ADC_PROTOCOL_OK;
}

void ads1220_scheduler_retry_current(ads1220_scheduler_t *scheduler)
{
  if (scheduler == 0)
  {
    return;
  }

  scheduler->running = 1U;
  scheduler->error_pending = 0U;
  scheduler->drdy_rearm_pending = 0U;
  scheduler->state = ADS1220_SCHED_SELECT_DEVICE;
  scheduler->last_status = ADC_PROTOCOL_OK;
}

void ads1220_scheduler_stop(ads1220_scheduler_t *scheduler)
{
  if (scheduler == 0)
  {
    return;
  }

  scheduler->running = 0U;
  scheduler->drdy_rearm_pending = 0U;
  adc_bus_cs_deassert();
}

void ads1220_scheduler_service(ads1220_scheduler_t *scheduler, bool selected_drdy_pending)
{
  ads1220_device_t *dev;
  uint8_t pixel_id;

  if ((scheduler == 0) || (scheduler->running == 0U))
  {
    return;
  }

  dev = ads1220_device_get(scheduler->current_device_id);
  if (dev == 0)
  {
    ads1220_scheduler_raise_error(scheduler, ADC_PROTOCOL_ERR_INVALID_ARG, scheduler->state);
    return;
  }

  switch (scheduler->state)
  {
    case ADS1220_SCHED_SELECT_DEVICE:
      adc_bus_select_device(scheduler->current_device_id);
      scheduler->drdy_rearm_pending = 1U;
      scheduler->state = ADS1220_SCHED_ENSURE_CONFIG;
      break;

    case ADS1220_SCHED_ENSURE_CONFIG:
      scheduler->last_status = ads1220_device_configure_channel(dev, scheduler->current_channel_id);
      if (scheduler->last_status == ADC_PROTOCOL_OK)
      {
        scheduler->state = ADS1220_SCHED_START_OR_ARM;
      }
      else
      {
        ads1220_scheduler_raise_error(scheduler, scheduler->last_status, ADS1220_SCHED_ENSURE_CONFIG);
      }
      break;

    case ADS1220_SCHED_START_OR_ARM:
      scheduler->last_status = ads1220_device_start_continuous(dev);
      scheduler->drdy_deadline_ms = HAL_GetTick() + APP_DRDY_TIMEOUT_MS;
      if (scheduler->last_status == ADC_PROTOCOL_OK)
      {
        scheduler->state = ADS1220_SCHED_WAIT_DRDY;
      }
      else
      {
        ads1220_scheduler_raise_error(scheduler, scheduler->last_status, ADS1220_SCHED_START_OR_ARM);
      }
      break;

    case ADS1220_SCHED_WAIT_DRDY:
      if ((selected_drdy_pending != false) || adc_bus_is_selected_drdy_low())
      {
        scheduler->state = ADS1220_SCHED_READ_SAMPLE;
      }
      else if ((int32_t)(HAL_GetTick() - scheduler->drdy_deadline_ms) >= 0)
      {
        ads1220_scheduler_raise_error(scheduler, ADC_PROTOCOL_ERR_TIMEOUT, ADS1220_SCHED_WAIT_DRDY);
      }
      break;

    case ADS1220_SCHED_READ_SAMPLE:
      scheduler->last_status = ads1220_device_read_sample(dev, &scheduler->pending_raw_code);
      if (scheduler->last_status == ADC_PROTOCOL_OK)
      {
        scheduler->state = ADS1220_SCHED_STORE_RESULT;
      }
      else
      {
        ads1220_scheduler_raise_error(scheduler, scheduler->last_status, ADS1220_SCHED_READ_SAMPLE);
      }
      break;

    case ADS1220_SCHED_STORE_RESULT:
      pixel_id = board_topology_pixel_id(scheduler->current_device_id, scheduler->current_channel_id);
      if ((scheduler->pixels != 0) && (pixel_id < PIXEL_COUNT))
      {
        scheduler->pixels[pixel_id] = scheduler->pending_raw_code;
      }
      scheduler->state = ADS1220_SCHED_ADVANCE_DEVICE;
      break;

    case ADS1220_SCHED_ADVANCE_DEVICE:
      ads1220_scheduler_advance_indices(scheduler);
      adc_bus_cs_deassert();
      scheduler->state = ADS1220_SCHED_SELECT_DEVICE;
      break;

    default:
      scheduler->state = ADS1220_SCHED_SELECT_DEVICE;
      break;
  }
}

uint8_t ads1220_scheduler_current_device_id(const ads1220_scheduler_t *scheduler)
{
  return (scheduler != 0) ? scheduler->current_device_id : 0U;
}

uint8_t ads1220_scheduler_current_channel_id(const ads1220_scheduler_t *scheduler)
{
  return (scheduler != 0) ? scheduler->current_channel_id : 0U;
}

bool ads1220_scheduler_frame_ready(const ads1220_scheduler_t *scheduler)
{
  return ((scheduler != 0) && (scheduler->frame_ready != 0U));
}

void ads1220_scheduler_clear_frame_ready(ads1220_scheduler_t *scheduler)
{
  if (scheduler != 0)
  {
    scheduler->frame_ready = 0U;
  }
}

bool ads1220_scheduler_has_error(const ads1220_scheduler_t *scheduler)
{
  return ((scheduler != 0) && (scheduler->error_pending != 0U));
}

ads1220_scheduler_state_t ads1220_scheduler_error_state(const ads1220_scheduler_t *scheduler)
{
  return (scheduler != 0) ? scheduler->error_state : ADS1220_SCHED_SELECT_DEVICE;
}

bool ads1220_scheduler_consume_drdy_rearm_request(ads1220_scheduler_t *scheduler)
{
  if ((scheduler == 0) || (scheduler->drdy_rearm_pending == 0U))
  {
    return false;
  }

  scheduler->drdy_rearm_pending = 0U;
  return true;
}
