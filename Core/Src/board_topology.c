#include "board_topology.h"

const ads1220_route_t g_ads1220_routes[ADC_DEVICE_COUNT] = {
  {0U, 0U, 0U},  {1U, 0U, 1U},  {2U, 0U, 2U},  {3U, 0U, 3U},  {4U, 0U, 4U},
  {5U, 1U, 0U},  {6U, 1U, 1U},  {7U, 1U, 2U},  {8U, 1U, 3U},  {9U, 1U, 4U},
  {10U, 2U, 0U}, {11U, 2U, 1U}, {12U, 2U, 2U}, {13U, 2U, 3U}, {14U, 2U, 4U},
  {15U, 3U, 0U}, {16U, 3U, 1U}, {17U, 3U, 2U}, {18U, 3U, 3U}, {19U, 3U, 4U},
  {20U, 4U, 0U}, {21U, 4U, 1U}, {22U, 4U, 2U}, {23U, 4U, 3U}, {24U, 4U, 4U}
};

const ads1220_route_t *board_topology_get_route(uint8_t device_id)
{
  if (device_id >= ADC_DEVICE_COUNT)
  {
    return 0;
  }

  return &g_ads1220_routes[device_id];
}

uint8_t board_topology_pixel_id(uint8_t device_id, uint8_t channel_id)
{
  if ((device_id >= ADC_DEVICE_COUNT) || (channel_id >= ADC_CHANNELS_PER_DEVICE))
  {
    return 0xFFU;
  }

  return (uint8_t)((device_id * ADC_CHANNELS_PER_DEVICE) + channel_id);
}

bool board_topology_get_pixel_route(uint16_t pixel_id, board_pixel_route_t *route)
{
  uint16_t pixel_in_subboard;

  if ((route == 0) || (pixel_id >= PIXEL_COUNT))
  {
    return false;
  }

  pixel_in_subboard = (uint16_t)(pixel_id % PIXELS_PER_SUBBOARD);

  route->pixel_id = pixel_id;
  route->row = (uint8_t)(pixel_id / ARRAY_WIDTH);
  route->column = (uint8_t)(pixel_id % ARRAY_WIDTH);
  route->subboard_id = (uint8_t)(pixel_id / PIXELS_PER_SUBBOARD);
  route->adc_id = (uint8_t)((pixel_id / ADC_CHANNELS_PER_DEVICE) % ADC_PER_SUBBOARD);
  route->adc_channel = (uint8_t)(pixel_in_subboard % ADC_CHANNELS_PER_DEVICE);
  return true;
}
