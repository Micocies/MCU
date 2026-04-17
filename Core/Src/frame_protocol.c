#include "frame_protocol.h"

#include <stddef.h>
#include <string.h>

/* 函数说明：
 *   执行 CRC-16/CCITT 的单字节更新。
 * 输入：
 *   crc: 当前 CRC 累计值。
 *   value: 需要加入计算的字节。
 * 输出：
 *   返回更新后的 CRC 值。
 * 作用：
 *   作为帧协议 CRC 计算的最小内部步骤，统一多处 CRC 累计逻辑。
 */
static uint16_t frame_protocol_crc16_update(uint16_t crc, uint8_t value)
{
  uint8_t bit;

  crc ^= (uint16_t)((uint16_t)value << 8);
  for (bit = 0U; bit < 8U; ++bit)
  {
    if ((crc & 0x8000U) != 0U)
    {
      crc = (uint16_t)((crc << 1) ^ 0x1021U);
    }
    else
    {
      crc = (uint16_t)(crc << 1);
    }
  }

  return crc;
}

/* 函数说明：
 *   填充 V1.0 固定图像帧头。
 * 输入：
 *   header: 输出帧头指针。
 *   frame_type: 当前帧类型。
 *   frame_id: 当前图像帧序号。
 *   timestamp_us: 当前帧时间戳，单位 us。
 * 输出：
 *   无。
 * 作用：
 *   统一初始化 magic、版本、10x10 尺寸、payload 长度和 CRC 占位字段。
 */
void frame_protocol_prepare_header(frame_header_t *header,
                                   frame_type_t frame_type,
                                   uint32_t frame_id,
                                   uint32_t timestamp_us)
{
  if (header == 0)
  {
    return;
  }

  memset(header, 0, sizeof(*header));
  header->magic = FRAME_PROTOCOL_MAGIC;
  header->version = FRAME_PROTOCOL_VERSION;
  header->frame_type = (uint8_t)frame_type;
  header->frame_id = frame_id;
  header->width = ARRAY_WIDTH;
  header->height = ARRAY_HEIGHT;
  header->timestamp_us = timestamp_us;
  header->payload_bytes = (uint16_t)FRAME_PROTOCOL_PAYLOAD_BYTES;
  header->crc16 = 0U;
}

/* 函数说明：
 *   对连续字节执行 CRC-16/CCITT 计算。
 * 输入：
 *   data: 待计算的数据指针。
 *   length: 待计算的数据长度，单位 byte。
 *   seed: CRC 初始值或前一次累计值。
 * 输出：
 *   返回累计后的 CRC 值。
 * 作用：
 *   为上位机可复现的 V1.0 帧 CRC 提供标准计算入口。
 */
uint16_t frame_protocol_crc16_ccitt(const uint8_t *data, uint32_t length, uint16_t seed)
{
  uint32_t i;
  uint16_t crc = seed;

  if (data == 0)
  {
    return crc;
  }

  for (i = 0U; i < length; ++i)
  {
    crc = frame_protocol_crc16_update(crc, data[i]);
  }

  return crc;
}

/* 函数说明：
 *   计算完整 V1.0 图像帧的 CRC。
 * 输入：
 *   frame: 待计算的图像帧指针。
 * 输出：
 *   返回帧 CRC；输入为空时返回 0。
 * 作用：
 *   按协议要求将 header 中 crc16 字段视为 0，再覆盖 header 和 400 字节 payload。
 */
uint16_t frame_protocol_compute_crc(const frame_packet_t *frame)
{
  uint32_t i;
  uint16_t crc = 0xFFFFU;
  const uint8_t *header_bytes;
  const uint8_t *payload_bytes;
  const uint32_t crc_offset = (uint32_t)offsetof(frame_header_t, crc16);

  if (frame == 0)
  {
    return 0U;
  }

  header_bytes = (const uint8_t *)&frame->header;
  for (i = 0U; i < sizeof(frame->header); ++i)
  {
    if ((i == crc_offset) || (i == (crc_offset + 1U)))
    {
      crc = frame_protocol_crc16_update(crc, 0U);
    }
    else
    {
      crc = frame_protocol_crc16_update(crc, header_bytes[i]);
    }
  }

  payload_bytes = (const uint8_t *)&frame->pixels[0];
  crc = frame_protocol_crc16_ccitt(payload_bytes, FRAME_PROTOCOL_PAYLOAD_BYTES, crc);
  return crc;
}

/* 函数说明：
 *   完成 V1.0 图像帧 CRC 写入。
 * 输入：
 *   frame: 待补齐 CRC 的图像帧指针。
 * 输出：
 *   无。
 * 作用：
 *   在 payload 已填充后计算 CRC，并写回 header.crc16。
 */
void frame_protocol_finalize(frame_packet_t *frame)
{
  if (frame == 0)
  {
    return;
  }

  frame->header.crc16 = 0U;
  frame->header.crc16 = frame_protocol_compute_crc(frame);
}

/* 函数说明：
 *   校验 V1.0 图像帧头和 CRC。
 * 输入：
 *   frame: 待校验的图像帧指针。
 * 输出：
 *   true : 帧头字段和 CRC 均符合当前 V1.0 协议。
 *   false: 输入为空、固定字段不匹配或 CRC 不匹配。
 * 作用：
 *   为单元测试和后续接收侧复用提供最小一致性检查。
 */
bool frame_protocol_validate(const frame_packet_t *frame)
{
  uint16_t expected_crc;

  if (frame == 0)
  {
    return false;
  }

  if ((frame->header.magic != FRAME_PROTOCOL_MAGIC) ||
      (frame->header.version != FRAME_PROTOCOL_VERSION) ||
      (frame->header.width != ARRAY_WIDTH) ||
      (frame->header.height != ARRAY_HEIGHT) ||
      (frame->header.payload_bytes != FRAME_PROTOCOL_PAYLOAD_BYTES))
  {
    return false;
  }

  expected_crc = frame_protocol_compute_crc(frame);
  return (expected_crc == frame->header.crc16);
}
