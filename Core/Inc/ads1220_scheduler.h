#ifndef __ADS1220_SCHEDULER_H
#define __ADS1220_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "adc_protocol.h"
#include "project_config.h"

typedef enum
{
  ADS1220_SCHED_SELECT_DEVICE = 0,
  ADS1220_SCHED_ENSURE_CONFIG,
  ADS1220_SCHED_START_OR_ARM,
  ADS1220_SCHED_WAIT_DRDY,
  ADS1220_SCHED_READ_SAMPLE,
  ADS1220_SCHED_STORE_RESULT,
  ADS1220_SCHED_ADVANCE_DEVICE
} ads1220_scheduler_state_t;

typedef struct
{
  ads1220_scheduler_state_t state;
  int32_t *pixels;
  uint8_t current_device_id;
  uint8_t current_channel_id;
  uint8_t frame_ready;
  uint8_t running;
  uint8_t error_pending;
  uint8_t drdy_rearm_pending;
  uint32_t drdy_deadline_ms;
  int32_t pending_raw_code;
  adc_protocol_status_t last_status;
  ads1220_scheduler_state_t error_state;
} ads1220_scheduler_t;

void ads1220_scheduler_init(ads1220_scheduler_t *scheduler, int32_t pixels[PIXEL_COUNT]);
void ads1220_scheduler_start(ads1220_scheduler_t *scheduler);
void ads1220_scheduler_retry_current(ads1220_scheduler_t *scheduler);
void ads1220_scheduler_stop(ads1220_scheduler_t *scheduler);
void ads1220_scheduler_service(ads1220_scheduler_t *scheduler, bool selected_drdy_pending);
uint8_t ads1220_scheduler_current_device_id(const ads1220_scheduler_t *scheduler);
uint8_t ads1220_scheduler_current_channel_id(const ads1220_scheduler_t *scheduler);
bool ads1220_scheduler_frame_ready(const ads1220_scheduler_t *scheduler);
void ads1220_scheduler_clear_frame_ready(ads1220_scheduler_t *scheduler);
bool ads1220_scheduler_has_error(const ads1220_scheduler_t *scheduler);
ads1220_scheduler_state_t ads1220_scheduler_error_state(const ads1220_scheduler_t *scheduler);
bool ads1220_scheduler_consume_drdy_rearm_request(ads1220_scheduler_t *scheduler);

#ifdef __cplusplus
}
#endif

#endif /* __ADS1220_SCHEDULER_H */
