#ifndef __PIXEL_MAP_H
#define __PIXEL_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "board_topology.h"

typedef board_pixel_route_t pixel_map_entry_t;

uint16_t pixel_map_make_id(uint8_t row, uint8_t column);
bool pixel_map_get_entry(uint16_t pixel_id, pixel_map_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif /* __PIXEL_MAP_H */
