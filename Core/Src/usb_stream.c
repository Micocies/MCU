#include "usb_stream.h"

#include <string.h>

#include "app_config.h"
#include "usbd_cdc_if.h"

/* FS 模式单包 64 字节，因此这里每次最多拼 2 帧 32 字节样本。 */
#define USB_STREAM_MAX_CHUNK_BYTES (sizeof(sample_packet_t) * 2U)

typedef struct
{
  /* 生产者是采样状态机，消费者是 usb_stream_service()。 */
  sample_packet_t queue[APP_USB_QUEUE_DEPTH];
  uint8_t tx_buffer[USB_STREAM_MAX_CHUNK_BYTES];
  uint32_t head;
  uint32_t tail;
  uint32_t count;
} usb_stream_context_t;

static usb_stream_context_t g_usb_stream;

/* 函数说明：
 *   初始化 USB 发送上下文。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   清空环形队列和临时发送缓冲区。
 */
void usb_stream_init(void)
{
  memset(&g_usb_stream, 0, sizeof(g_usb_stream));
}

/* 函数说明：
 *   将样本帧写入 USB 环形队列。
 * 输入：
 *   pkt: 待发送的样本帧。
 * 输出：
 *   true : 成功入队且未发生丢包。
 *   false: 入队失败或发生了“丢弃最旧帧”的情况。
 * 作用：
 *   队列满时优先保留最新数据。
 */
bool usb_stream_enqueue(const sample_packet_t *pkt)
{
  sample_packet_t local_packet;
  bool dropped_oldest = false;

  if (pkt == NULL)
  {
    return false;
  }

  local_packet = *pkt;

  if (g_usb_stream.count >= APP_USB_QUEUE_DEPTH)
  {
    /* 队列满时丢弃最旧帧，保证最新采样结果优先保留。 */
    g_usb_stream.tail = (g_usb_stream.tail + 1U) % APP_USB_QUEUE_DEPTH;
    g_usb_stream.count--;
    dropped_oldest = true;
    local_packet.flags |= SAMPLE_FLAG_USB_OVERFLOW;
  }

  g_usb_stream.queue[g_usb_stream.head] = local_packet;
  g_usb_stream.head = (g_usb_stream.head + 1U) % APP_USB_QUEUE_DEPTH;
  g_usb_stream.count++;

  return (dropped_oldest == false);
}

/* 函数说明：
 *   推动 USB 队列发送。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   尝试把队列头部样本打包后通过 USB CDC 发送出去。
 */
void usb_stream_service(void)
{
  uint32_t packet_count;
  uint16_t tx_len;
  uint8_t tx_status;

  if (g_usb_stream.count == 0U)
  {
    return;
  }

  /* 两帧打包能正好凑满 64 字节 CDC FS 负载。 */
  packet_count = (g_usb_stream.count >= 2U) ? 2U : 1U;
  tx_len = (uint16_t)(packet_count * sizeof(sample_packet_t));

  memcpy(&g_usb_stream.tx_buffer[0],
         &g_usb_stream.queue[g_usb_stream.tail],
         sizeof(sample_packet_t));

  if (packet_count == 2U)
  {
    memcpy(&g_usb_stream.tx_buffer[sizeof(sample_packet_t)],
           &g_usb_stream.queue[(g_usb_stream.tail + 1U) % APP_USB_QUEUE_DEPTH],
           sizeof(sample_packet_t));
  }

  tx_status = CDC_Transmit_FS(g_usb_stream.tx_buffer, tx_len);
  if (tx_status != USBD_OK)
  {
    /* USB 忙时直接返回，等待下轮主循环继续尝试。 */
    return;
  }

  g_usb_stream.tail = (g_usb_stream.tail + packet_count) % APP_USB_QUEUE_DEPTH;
  g_usb_stream.count -= packet_count;
}

