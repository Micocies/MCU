#include "frame_builder.h"

#include <string.h>

#include "pixel_map.h"

/* 函数说明：
 *   生成指定像素在测试图案中的确定性数值。
 * 输入：
 *   pixel_id: 像素编号，范围为 0 到 PIXEL_COUNT-1。
 *   frame_id: 当前图像帧序号，用于给测试图案加入低幅度动态变化。
 * 输出：
 *   返回该像素对应的 int32_t 测试值。
 * 作用：
 *   为测试帧和半真实帧中未接入真实通道的位置提供稳定、可定位的占位值。
 */
static int32_t frame_builder_test_value(uint16_t pixel_id, uint32_t frame_id)
{
  pixel_map_entry_t entry;
  int32_t row_component = 0;
  int32_t column_component = 0;

  if (pixel_map_get_entry(pixel_id, &entry) != false)
  {
    row_component = (int32_t)entry.row * 1000;
    column_component = (int32_t)entry.column * 10;
  }

  return row_component + column_component + (int32_t)(frame_id & 0x0FU);
}

/* 函数说明：
 *   填充整帧的 10x10 测试图案。
 * 输入：
 *   frame: 待填充的图像帧指针。
 *   frame_id: 当前图像帧序号。
 * 输出：
 *   无。
 * 作用：
 *   将 100 个像素全部写入确定性 row/column 测试值，便于上位机验证像素位置映射。
 */
static void frame_builder_fill_test_pattern(frame_packet_t *frame, uint32_t frame_id)
{
  uint16_t pixel_id;

  for (pixel_id = 0U; pixel_id < PIXEL_COUNT; ++pixel_id)
  {
    frame->pixels[pixel_id] = frame_builder_test_value(pixel_id, frame_id);
  }
}

/* 函数说明：
 *   初始化帧构造器上下文。
 * 输入：
 *   builder: 帧构造器上下文指针。
 *   mode: 默认输出帧类型。
 * 输出：
 *   无。
 * 作用：
 *   设置 V1.0 整帧输出模式，并清空最近一次真实样本缓存。
 */
void frame_builder_init(frame_builder_t *builder, frame_type_t mode)
{
  if (builder == 0)
  {
    return;
  }

  builder->mode = mode;
  builder->last_real_sample = 0;
}

/* 函数说明：
 *   切换帧构造器输出模式。
 * 输入：
 *   builder: 帧构造器上下文指针。
 *   mode: 新的输出帧类型。
 * 输出：
 *   无。
 * 作用：
 *   支持在测试帧、半真实帧和占位帧之间切换，不改变主机侧固定帧格式。
 */
void frame_builder_set_mode(frame_builder_t *builder, frame_type_t mode)
{
  if (builder == 0)
  {
    return;
  }

  builder->mode = mode;
}

/* 函数说明：
 *   构造一帧固定 100 pixels 图像数据。
 * 输入：
 *   builder: 帧构造器上下文指针，可为空；为空时按半真实帧处理。
 *   frame: 输出图像帧指针。
 *   frame_id: 当前图像帧序号。
 *   timestamp_us: 当前帧时间戳，单位 us。
 *   real_sample: 当前采样链路给出的真实校正值。
 * 输出：
 *   无。
 * 作用：
 *   按当前模式填充 payload，并补齐 V1.0 帧头和 CRC。
 */
void frame_builder_build(frame_builder_t *builder,
                         frame_packet_t *frame,
                         uint32_t frame_id,
                         uint32_t timestamp_us,
                         int32_t real_sample)
{
  frame_type_t mode = FRAME_TYPE_PARTIAL_REAL;

  if (frame == 0)
  {
    return;
  }

  if (builder != 0)
  {
    builder->last_real_sample = real_sample;
    mode = builder->mode;
  }

  memset(frame, 0, sizeof(*frame));
  frame_protocol_prepare_header(&frame->header, mode, frame_id, timestamp_us);

  if (mode == FRAME_TYPE_TEST)
  {
    frame_builder_fill_test_pattern(frame, frame_id);
  }
  else if (mode == FRAME_TYPE_PLACEHOLDER)
  {
    memset(&frame->pixels[0], 0, sizeof(frame->pixels));
  }
  else
  {
    frame_builder_fill_test_pattern(frame, frame_id);
    frame->pixels[PROJECT_ACTIVE_PIXEL_ID] = real_sample;
  }

  frame_protocol_finalize(frame);
}

void frame_builder_build_pixels(frame_builder_t *builder,
                                frame_packet_t *frame,
                                uint32_t frame_id,
                                uint32_t timestamp_us,
                                const int32_t pixels[PIXEL_COUNT])
{
  frame_type_t mode = FRAME_TYPE_PARTIAL_REAL;

  if (frame == 0)
  {
    return;
  }

  if (builder != 0)
  {
    mode = builder->mode;
  }

  memset(frame, 0, sizeof(*frame));
  frame_protocol_prepare_header(&frame->header, mode, frame_id, timestamp_us);

  if (mode == FRAME_TYPE_TEST)
  {
    frame_builder_fill_test_pattern(frame, frame_id);
  }
  else if ((mode == FRAME_TYPE_PLACEHOLDER) || (pixels == 0))
  {
    memset(&frame->pixels[0], 0, sizeof(frame->pixels));
  }
  else if (mode == FRAME_TYPE_PARTIAL_REAL)
  {
    frame_builder_fill_test_pattern(frame, frame_id);
    frame->pixels[PROJECT_ACTIVE_PIXEL_ID] = pixels[PROJECT_ACTIVE_PIXEL_ID];
  }
  else
  {
    memcpy(&frame->pixels[0], pixels, sizeof(frame->pixels));
  }

  frame_protocol_finalize(frame);
}
