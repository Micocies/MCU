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
  uint16_t pixel_id;
  uint8_t row;
  uint8_t column;
  uint8_t subboard_id;
  uint8_t adc_id;
  uint8_t adc_channel;
} board_pixel_route_t;

bool board_topology_get_route(uint16_t pixel_id, board_pixel_route_t *route);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_TOPOLOGY_H */
