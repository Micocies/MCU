#include "fake_usb.h"

#include <string.h>

static uint8_t g_transmit_status = USBD_OK;
static uint32_t g_transmit_count;
static uint16_t g_last_len;
static uint8_t g_last_data[64];

/* 函数说明：
 *   重置 fake USB 发送记录。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   清空 CDC 返回状态、发送次数、最后一次长度和发送内容，保证用例隔离。
 */
void fake_usb_reset(void)
{
  g_transmit_status = USBD_OK;
  g_transmit_count = 0U;
  g_last_len = 0U;
  memset(g_last_data, 0, sizeof(g_last_data));
}

/* 函数说明：
 *   设置 fake CDC 发送返回状态。
 * 输入：
 *   status: CDC_Transmit_FS 返回值。
 * 输出：
 *   无。
 * 作用：
 *   模拟 USBD_OK、USBD_BUSY 或失败，验证 usb_stream 的重试策略。
 */
void fake_usb_set_transmit_status(uint8_t status)
{
  g_transmit_status = status;
}

/* 函数说明：
 *   获取 CDC 发送调用次数。
 * 输入：
 *   无。
 * 输出：
 *   返回 CDC_Transmit_FS 被调用次数。
 * 作用：
 *   用于断言 service 是否触发真实发送动作。
 */
uint32_t fake_usb_get_transmit_count(void)
{
  return g_transmit_count;
}

/* 函数说明：
 *   获取最后一次 CDC 发送长度。
 * 输入：
 *   无。
 * 输出：
 *   返回最后一次发送的字节数。
 * 作用：
 *   用于验证 32 byte 单包和 64 byte 双包聚合策略。
 */
uint16_t fake_usb_get_last_len(void)
{
  return g_last_len;
}

/* 函数说明：
 *   获取最后一次 CDC 发送原始字节。
 * 输入：
 *   无。
 * 输出：
 *   返回只读发送缓冲区指针。
 * 作用：
 *   允许测试按字节检查发送内容。
 */
const uint8_t *fake_usb_get_last_data(void)
{
  return g_last_data;
}

/* 函数说明：
 *   获取最后一次 CDC 发送内容的包视图。
 * 输入：
 *   无。
 * 输出：
 *   返回 sample_packet_t 指针。
 * 作用：
 *   允许测试直接断言 32 byte 帧中的 sequence、flags 和负载字段。
 */
const sample_packet_t *fake_usb_get_last_packets(void)
{
  return (const sample_packet_t *)g_last_data;
}

/* 函数说明：
 *   fake CDC 发送函数。
 * 输入：
 *   Buf: 待发送缓冲区。
 *   Len: 待发送字节数。
 * 输出：
 *   返回预设 USB 发送状态。
 * 作用：
 *   记录最后一次发送内容，不依赖真实 USB 设备。
 */
uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
  g_transmit_count++;
  g_last_len = Len;
  memset(g_last_data, 0, sizeof(g_last_data));
  if ((Buf != 0) && (Len <= sizeof(g_last_data)))
  {
    memcpy(g_last_data, Buf, Len);
  }

  return g_transmit_status;
}
