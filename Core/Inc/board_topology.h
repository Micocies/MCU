#ifndef __BOARD_TOPOLOGY_H
#define __BOARD_TOPOLOGY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "project_config.h"

typedef struct
{
  uint8_t device_id;
  uint8_t subboard_id;
  uint8_t local_device_id;
} ads1220_route_t;

typedef struct
{
  uint16_t pixel_id;
  uint8_t row;
  uint8_t column;
  uint8_t subboard_id;
  uint8_t adc_id;
  uint8_t adc_channel;
} board_pixel_route_t;

extern const ads1220_route_t g_ads1220_routes[ADC_DEVICE_COUNT];

const ads1220_route_t *board_topology_get_route(uint8_t device_id);
uint8_t board_topology_pixel_id(uint8_t device_id, uint8_t channel_id);
bool board_topology_get_pixel_route(uint16_t pixel_id, board_pixel_route_t *route);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_TOPOLOGY_H */
