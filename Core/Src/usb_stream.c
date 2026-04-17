#include "usb_stream.h"

#include <string.h>

#include "app_config.h"
#include "usbd_cdc_if.h"

#define USB_STREAM_MAX_PACKET_BYTES (sizeof(frame_packet_t))

typedef struct
{
  uint16_t length;
  uint8_t data[USB_STREAM_MAX_PACKET_BYTES];
} usb_stream_queued_packet_t;

typedef struct
{
  usb_stream_queued_packet_t *packet;
  uint32_t depth;
  uint32_t head;
  uint32_t tail;
  uint32_t count;
} usb_stream_ring_t;

typedef struct
{
  usb_stream_ring_t frame_ring;
  usb_stream_ring_t aux_ring;
  usb_stream_stats_t stats;
  uint8_t tx_buffer[USB_STREAM_MAX_PACKET_BYTES];
} usb_stream_context_t;

static usb_stream_queued_packet_t g_frame_packets[APP_USB_FRAME_QUEUE_DEPTH];
static usb_stream_queued_packet_t g_aux_packets[APP_USB_AUX_QUEUE_DEPTH];
static usb_stream_context_t g_usb_stream;

/* 函数说明：
 *   绑定 USB stream 内部环形队列的静态存储区。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   在上下文清零后恢复 frame/aux 两个队列对静态数组的引用和深度配置。
 */
static void usb_stream_bind_rings(void)
{
  g_usb_stream.frame_ring.packet = g_frame_packets;
  g_usb_stream.frame_ring.depth = APP_USB_FRAME_QUEUE_DEPTH;
  g_usb_stream.aux_ring.packet = g_aux_packets;
  g_usb_stream.aux_ring.depth = APP_USB_AUX_QUEUE_DEPTH;
}

/* 函数说明：
 *   将一段待发送数据写入指定 USB 环形队列。
 * 输入：
 *   ring: 目标环形队列指针。
 *   data: 待入队的原始字节指针。
 *   length: 待入队数据长度，单位 byte。
 * 输出：
 *   返回 USB 入队结果码。
 * 作用：
 *   为图像帧队列和辅助队列提供统一入队逻辑；队列满时丢弃最旧数据。
 */
static usb_stream_enqueue_result_t usb_stream_enqueue_internal(usb_stream_ring_t *ring,
                                                               const uint8_t *data,
                                                               uint16_t length)
{
  bool dropped_oldest = false;

  if ((ring == 0) || (ring->packet == 0) || (data == 0) ||
      (length == 0U) || (length > USB_STREAM_MAX_PACKET_BYTES))
  {
    return USB_STREAM_ENQUEUE_ERR_INVALID_ARG;
  }

  if (ring->count >= ring->depth)
  {
    ring->tail = (ring->tail + 1U) % ring->depth;
    ring->count--;
    dropped_oldest = true;
  }

  ring->packet[ring->head].length = length;
  memcpy(&ring->packet[ring->head].data[0], data, length);
  ring->head = (ring->head + 1U) % ring->depth;
  ring->count++;

  return (dropped_oldest == false) ? USB_STREAM_ENQUEUE_OK : USB_STREAM_ENQUEUE_OK_DROPPED_OLDEST;
}

/* 函数说明：
 *   弹出指定环形队列的队首数据。
 * 输入：
 *   ring: 目标环形队列指针。
 * 输出：
 *   无。
 * 作用：
 *   在 CDC 发送成功后推进队列 tail，并减少待发送计数。
 */
static void usb_stream_pop_packet(usb_stream_ring_t *ring)
{
  ring->tail = (ring->tail + 1U) % ring->depth;
  ring->count--;
}

/* 函数说明：
 *   初始化 USB 发送上下文。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   清空图像帧队列、辅助队列、统计信息和临时发送缓冲区。
 */
void usb_stream_init(void)
{
  memset(&g_usb_stream, 0, sizeof(g_usb_stream));
  memset(g_frame_packets, 0, sizeof(g_frame_packets));
  memset(g_aux_packets, 0, sizeof(g_aux_packets));
  usb_stream_bind_rings();
}

/* 函数说明：
 *   将 10x10 图像帧写入主发送队列。
 * 输入：
 *   frame: 待发送的 V1.0 固定图像帧指针。
 * 输出：
 *   返回 USB 入队结果码。
 * 作用：
 *   主数据面使用最新帧优先策略；队列满时丢弃最旧图像帧并记录统计。
 */
usb_stream_enqueue_result_t usb_stream_enqueue_frame(const frame_packet_t *frame)
{
  usb_stream_enqueue_result_t result;

  result = usb_stream_enqueue_internal(&g_usb_stream.frame_ring,
                                       (const uint8_t *)frame,
                                       (uint16_t)sizeof(*frame));
  if (result != USB_STREAM_ENQUEUE_ERR_INVALID_ARG)
  {
    g_usb_stream.stats.frame_enqueued++;
    if (result == USB_STREAM_ENQUEUE_OK_DROPPED_OLDEST)
    {
      g_usb_stream.stats.frame_overflow++;
    }
  }

  return result;
}

/* 函数说明：
 *   将元信息或故障辅助帧写入辅助发送队列。
 * 输入：
 *   pkt: 待发送的 32 字节辅助帧指针。
 * 输出：
 *   返回 USB 入队结果码。
 * 作用：
 *   辅助面与图像帧队列隔离；队列满时标记 USB overflow 并丢弃最旧辅助帧。
 */
usb_stream_enqueue_result_t usb_stream_enqueue_aux(const sample_packet_t *pkt)
{
  sample_packet_t local_packet;
  usb_stream_enqueue_result_t result;

  if (pkt == 0)
  {
    return USB_STREAM_ENQUEUE_ERR_INVALID_ARG;
  }

  local_packet = *pkt;
  if (g_usb_stream.aux_ring.count >= g_usb_stream.aux_ring.depth)
  {
    local_packet.flags |= SAMPLE_FLAG_USB_OVERFLOW;
  }

  result = usb_stream_enqueue_internal(&g_usb_stream.aux_ring,
                                       (const uint8_t *)&local_packet,
                                       (uint16_t)sizeof(local_packet));
  if (result != USB_STREAM_ENQUEUE_ERR_INVALID_ARG)
  {
    g_usb_stream.stats.aux_enqueued++;
    if (result == USB_STREAM_ENQUEUE_OK_DROPPED_OLDEST)
    {
      g_usb_stream.stats.aux_overflow++;
    }
  }

  return result;
}

/* 函数说明：
 *   推动 USB 队列发送。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   优先发送图像帧；没有图像帧时发送辅助帧。USB busy 时保留队首数据等待重试。
 */
void usb_stream_service(void)
{
  usb_stream_ring_t *active_ring;
  uint16_t tx_len;
  uint8_t tx_status;

  if (g_usb_stream.frame_ring.count != 0U)
  {
    active_ring = &g_usb_stream.frame_ring;
  }
  else if (g_usb_stream.aux_ring.count != 0U)
  {
    active_ring = &g_usb_stream.aux_ring;
  }
  else
  {
    return;
  }

  tx_len = active_ring->packet[active_ring->tail].length;
  memcpy(&g_usb_stream.tx_buffer[0], &active_ring->packet[active_ring->tail].data[0], tx_len);

  tx_status = CDC_Transmit_FS(g_usb_stream.tx_buffer, tx_len);
  g_usb_stream.stats.last_tx_status = tx_status;
  if (tx_status != USBD_OK)
  {
    if (tx_status == USBD_BUSY)
    {
      g_usb_stream.stats.tx_busy++;
    }
    else
    {
      g_usb_stream.stats.tx_error++;
    }
    return;
  }

  g_usb_stream.stats.tx_ok++;
  usb_stream_pop_packet(active_ring);
}

/* 函数说明：
 *   获取 USB 发送统计快照。
 * 输入：
 *   stats: 输出统计信息指针。
 * 输出：
 *   无。
 * 作用：
 *   向应用层或测试代码暴露队列入队、溢出和 CDC 发送状态。
 */
void usb_stream_get_stats(usb_stream_stats_t *stats)
{
  if (stats == 0)
  {
    return;
  }

  *stats = g_usb_stream.stats;
}

#ifdef UNIT_TEST
/* 函数说明：
 *   获取当前图像帧队列深度。
 * 输入：
 *   无。
 * 输出：
 *   返回 frame ring 中待发送图像帧数量。
 * 作用：
 *   仅在 UNIT_TEST 构建下提供只读观察口，用于验证队列策略。
 */
uint32_t usb_stream_test_get_frame_count(void)
{
  return g_usb_stream.frame_ring.count;
}

/* 函数说明：
 *   获取当前辅助队列深度。
 * 输入：
 *   无。
 * 输出：
 *   返回 aux ring 中待发送元信息或故障帧数量。
 * 作用：
 *   仅在 UNIT_TEST 构建下提供只读观察口，用于验证 frame/aux 队列隔离。
 */
uint32_t usb_stream_test_get_aux_count(void)
{
  return g_usb_stream.aux_ring.count;
}
#endif
