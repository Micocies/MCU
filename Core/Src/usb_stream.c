#include "usb_stream.h"

#include <string.h>

#include "app_config.h"
#include "usbd_cdc_if.h"

/* FS 模式单包 64 字节，因此这里每次最多拼 2 帧 32 字节数据。
 * 样本与辅助帧分队列管理，避免命令/诊断流打断样本连续性。 */
#define USB_STREAM_MAX_CHUNK_BYTES (sizeof(sample_packet_t) * 2U)

typedef struct
{
  sample_packet_t packet[APP_USB_QUEUE_DEPTH];
  uint32_t head;
  uint32_t tail;
  uint32_t count;
} usb_stream_ring_t;

typedef struct
{
  /* 当前默认前提：
   * 生产者和消费者都在主循环上下文里访问，不支持 ISR 并发访问。 */
  usb_stream_ring_t sample_ring;
  usb_stream_ring_t aux_ring;
  uint8_t tx_buffer[USB_STREAM_MAX_CHUNK_BYTES];
  uint32_t sample_wait_start_ms;
} usb_stream_context_t;

static usb_stream_context_t g_usb_stream;

static usb_stream_enqueue_result_t usb_stream_enqueue_internal(usb_stream_ring_t *ring,
                                                               uint32_t ring_depth,
                                                               const sample_packet_t *pkt)
{
  sample_packet_t local_packet;
  bool dropped_oldest = false;

  if ((ring == NULL) || (pkt == NULL))
  {
    return USB_STREAM_ENQUEUE_ERR_INVALID_ARG;
  }

  local_packet = *pkt;

  if (ring->count >= ring_depth)
  {
    ring->tail = (ring->tail + 1U) % ring_depth;
    ring->count--;
    dropped_oldest = true;
    local_packet.flags |= SAMPLE_FLAG_USB_OVERFLOW;
  }

  ring->packet[ring->head] = local_packet;
  ring->head = (ring->head + 1U) % ring_depth;
  ring->count++;

  return (dropped_oldest == false) ? USB_STREAM_ENQUEUE_OK : USB_STREAM_ENQUEUE_OK_DROPPED_OLDEST;
}

static bool usb_stream_has_elapsed(uint32_t start_ms, uint32_t duration_ms)
{
  return ((HAL_GetTick() - start_ms) >= duration_ms);
}

static void usb_stream_mark_sample_pending_start(void)
{
  if (g_usb_stream.sample_ring.count == 1U)
  {
    g_usb_stream.sample_wait_start_ms = HAL_GetTick();
  }
}

static uint32_t usb_stream_select_packet_count(const usb_stream_ring_t *ring, bool allow_batch_wait)
{
  if ((ring == NULL) || (ring->count == 0U))
  {
    return 0U;
  }

  if (ring->count >= 2U)
  {
    return 2U;
  }

  if (allow_batch_wait != false)
  {
    if (!usb_stream_has_elapsed(g_usb_stream.sample_wait_start_ms, APP_USB_BATCH_MAX_WAIT_MS))
    {
      return 0U;
    }
  }

  return 1U;
}

static void usb_stream_copy_packets(const usb_stream_ring_t *ring, uint32_t ring_depth, uint32_t packet_count)
{
  memcpy(&g_usb_stream.tx_buffer[0],
         &ring->packet[ring->tail],
         sizeof(sample_packet_t));

  if (packet_count == 2U)
  {
    memcpy(&g_usb_stream.tx_buffer[sizeof(sample_packet_t)],
           &ring->packet[(ring->tail + 1U) % ring_depth],
           sizeof(sample_packet_t));
  }
}

static void usb_stream_pop_packets(usb_stream_ring_t *ring, uint32_t ring_depth, uint32_t packet_count)
{
  ring->tail = (ring->tail + packet_count) % ring_depth;
  ring->count -= packet_count;
}

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
 *   将样本帧写入样本队列。
 * 输入：
 *   pkt: 待发送的样本帧。
 * 输出：
 *   返回 USB 入队结果码。
 * 作用：
 *   队列满时仅影响样本队列，并为新帧标记溢出标志。
 */
usb_stream_enqueue_result_t usb_stream_enqueue_sample(const sample_packet_t *pkt)
{
  usb_stream_enqueue_result_t result;

  result = usb_stream_enqueue_internal(&g_usb_stream.sample_ring, APP_USB_QUEUE_DEPTH, pkt);
  if (result != USB_STREAM_ENQUEUE_ERR_INVALID_ARG)
  {
    usb_stream_mark_sample_pending_start();
  }
  return result;
}

/* 函数说明：
 *   将元信息或故障帧写入辅助队列。
 * 输入：
 *   pkt: 待发送的数据帧。
 * 输出：
 *   返回 USB 入队结果码。
 * 作用：
 *   控制面/诊断面与样本队列隔离，避免引入样本序号间断。
 */
usb_stream_enqueue_result_t usb_stream_enqueue_aux(const sample_packet_t *pkt)
{
  return usb_stream_enqueue_internal(&g_usb_stream.aux_ring, APP_USB_AUX_QUEUE_DEPTH, pkt);
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
  usb_stream_ring_t *active_ring;
  uint32_t active_depth;
  uint32_t packet_count;
  uint16_t tx_len;
  uint8_t tx_status;

  if (g_usb_stream.sample_ring.count != 0U)
  {
    packet_count = usb_stream_select_packet_count(&g_usb_stream.sample_ring, true);
    if (packet_count == 0U)
    {
      return;
    }
    active_ring = &g_usb_stream.sample_ring;
    active_depth = APP_USB_QUEUE_DEPTH;
  }
  else if (g_usb_stream.aux_ring.count != 0U)
  {
    packet_count = usb_stream_select_packet_count(&g_usb_stream.aux_ring, false);
    active_ring = &g_usb_stream.aux_ring;
    active_depth = APP_USB_AUX_QUEUE_DEPTH;
  }
  else
  {
    return;
  }

  tx_len = (uint16_t)(packet_count * sizeof(sample_packet_t));
  usb_stream_copy_packets(active_ring, active_depth, packet_count);

  tx_status = CDC_Transmit_FS(g_usb_stream.tx_buffer, tx_len);
  if (tx_status != USBD_OK)
  {
    /* USB 忙时直接返回，等待下轮主循环继续尝试。 */
    return;
  }

  usb_stream_pop_packets(active_ring, active_depth, packet_count);
  if ((active_ring == &g_usb_stream.sample_ring) && (g_usb_stream.sample_ring.count != 0U))
  {
    usb_stream_mark_sample_pending_start();
  }
}

