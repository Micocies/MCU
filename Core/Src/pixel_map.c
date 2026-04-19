#include "pixel_map.h"

/* 函数说明：
 *   根据行列坐标生成固定 row-major pixel_id。
 * 输入：
 *   row: 像素行号，范围为 0 到 ARRAY_HEIGHT-1。
 *   column: 像素列号，范围为 0 到 ARRAY_WIDTH-1。
 * 输出：
 *   返回有效 pixel_id；行列越界时返回 PIXEL_COUNT 作为无效标记。
 * 作用：
 *   为上位机和固件侧统一 10x10 像素编号规则。
 */
uint16_t pixel_map_make_id(uint8_t row, uint8_t column)
{
  if ((row >= ARRAY_HEIGHT) || (column >= ARRAY_WIDTH))
  {
    return PIXEL_COUNT;
  }

  return (uint16_t)(((uint16_t)row * ARRAY_WIDTH) + column);
}

/* 函数说明：
 *   查询指定 pixel_id 的像素映射条目。
 * 输入：
 *   pixel_id: 像素编号，范围为 0 到 PIXEL_COUNT-1。
 *   entry: 输出像素映射条目指针。
 * 输出：
 *   true : 查询成功，entry 已填充。
 *   false: 参数为空或 pixel_id 越界。
 * 作用：
 *   对外提供像素映射入口，内部复用固定板级拓扑映射。
 */
bool pixel_map_get_entry(uint16_t pixel_id, pixel_map_entry_t *entry)
{
  return board_topology_get_pixel_route(pixel_id, entry);
}
