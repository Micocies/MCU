#include "board_topology.h"

/* 函数说明：
 *   查询指定 pixel_id 对应的固定硬件拓扑路由。
 * 输入：
 *   pixel_id: 像素编号，范围为 0 到 PIXEL_COUNT-1。
 *   route: 输出路由信息指针。
 * 输出：
 *   true : 查询成功，route 已填充。
 *   false: 参数为空或 pixel_id 超出固定 100 pixels 范围。
 * 作用：
 *   将 V1.0 冻结的 row-major 像素编号映射到子板、ADS1220 和通道位置。
 */
bool board_topology_get_route(uint16_t pixel_id, board_pixel_route_t *route)
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
