#ifndef __ADS1220_DEVICE_H
#define __ADS1220_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "adc_protocol.h"
#include "board_topology.h"

typedef struct
{
  uint8_t device_id;
  uint8_t subboard_id;
  uint8_t local_device_id;
  ads1220_config_t expected_config;
  int32_t last_raw_code;
  adc_protocol_status_t last_status;
  uint8_t is_configured;
} ads1220_device_t;

void ads1220_device_table_init(void);
ads1220_device_t *ads1220_device_get(uint8_t device_id);
adc_protocol_status_t ads1220_device_reset(ads1220_device_t *dev);
adc_protocol_status_t ads1220_device_configure_default(ads1220_device_t *dev);
adc_protocol_status_t ads1220_device_configure_channel(ads1220_device_t *dev, uint8_t channel_id);
adc_protocol_status_t ads1220_device_start_continuous(ads1220_device_t *dev);
adc_protocol_status_t ads1220_device_read_sample(ads1220_device_t *dev, int32_t *raw_code);
adc_protocol_status_t ads1220_device_link_check(ads1220_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* __ADS1220_DEVICE_H */
