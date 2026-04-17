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
  usb_stream_stats_t stats;
  uint8_t tx_buffer[USB_STREAM_MAX_CHUNK_BYTES];
  uint32_t sample_wait_start_ms;
} usb_stream_context_t;

static usb_stream_context_t g_usb_stream;
/* 函数说明：
 *   初始化 USB 发送上下文。
 * 输入：
 *   usb_stream_ring_t *ring 环形队列指针
 *   uint32_t ring_depth 队列深度
 *   const sample_packet_t *pkt 待发送的数据包指针
 * 输出：
 *   usb_stream_enqueue_result_t 枚举值，入队结果码
 * 作用：
 *   将数据包写入指定环形队列，队列满时丢弃最旧数据并为新包标记溢出标志。
 */
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
/* 函数说明：
 *   判断从起始时刻到现在是否已经超过指定时长。
 * 输入：
 *   uint32_t start_ms 起始时刻
 *   uint32_t duration_ms 指定时间间隔
 * 输出：
 *   bool 返回值，表示是否超过指定时长
 * 作用：
 *   同函数说明
 */
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
/* 函数说明：
 *   选择当前待发送的数据包数量。
 * 输入：
 *   const usb_stream_ring_t *ring 待检查的环形队列指针
 *   bool allow_batch_wait 是否允许等待更多数据以组成批量发送
 * 输出：
 *   uint32_t 返回值，表示当前可发送的数据包数量（0、1 或 2）
 * 作用：
 *   根据队列状态和等待时间判断当前是否可以发送数据包，支持单包或批量发送以优化 USB 带宽利用率。
 *   允许批量等待时，如果队列中只有 1 个包但未超过最大等待时间，则返回 0，等待更多数据到来以组成批量发送。
 *   否则返回当前可发送的包数量（最多 2 个）。
 */
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
/* 函数说明：
 *   将数据包从环形队列复制到 USB 发送缓冲区。
 * 输入：
 *   const usb_stream_ring_t *ring 待复制的环形队列指针
 *   uint32_t ring_depth 队列深度
 *   uint32_t packet_count 要复制的数据包数量
 * 输出：
 *   无
 * 作用：
 *   从队列的 tail 开始复制指定数量的数据包到 USB 发送缓冲区，支持单包或批量复制以优化 USB 带宽利用率。
 *   复制时考虑环形队列的循环特性，确保正确处理队列尾部和头部之间的边界情况。
 *   复制完成后，USB 发送缓冲区将包含要发送的数据包，可以直接用于 USB CDC 传输。
 */
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
/* 函数说明：
 *   从环形队列中弹出已发送的数据包。
 * 输入：
 *   usb_stream_ring_t *ring 待弹出的环形队列指针 
 *   uint32_t ring_depth 队列深度
 *   uint32_t packet_count 要弹出的数据包数量
 * 输出：
 *    无
 * 作用：
 *   同函数说明。
 */
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
    g_usb_stream.stats.sample_enqueued++;
    if (result == USB_STREAM_ENQUEUE_OK_DROPPED_OLDEST)
    {
      g_usb_stream.stats.sample_overflow++;
    }
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
  usb_stream_enqueue_result_t result;

  result = usb_stream_enqueue_internal(&g_usb_stream.aux_ring, APP_USB_AUX_QUEUE_DEPTH, pkt);
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
    /* USB 忙时直接返回，等待下轮主循环继续尝试。 */
    return;
  }

  g_usb_stream.stats.tx_ok++;
  usb_stream_pop_packets(active_ring, active_depth, packet_count);
  if ((active_ring == &g_usb_stream.sample_ring) && (g_usb_stream.sample_ring.count != 0U))
  {
    usb_stream_mark_sample_pending_start();
  }
}

void usb_stream_get_stats(usb_stream_stats_t *stats)
{
  if (stats == NULL)
  {
    return;
  }

  *stats = g_usb_stream.stats;
}

#ifdef UNIT_TEST
/* 函数说明：
 *   获取样本队列当前深度。
 * 输入：
 *   无。
 * 输出：
 *   返回 sample ring 中待发送样本帧数量。
 * 作用：
 *   仅在 UNIT_TEST 构建下提供只读观察口，用于验证发送成功、USB busy 等队列策略。
 */
uint32_t usb_stream_test_get_sample_count(void)
{
  return g_usb_stream.sample_ring.count;
}

/* 函数说明：
 *   获取辅助队列当前深度。
 * 输入：
 *   无。
 * 输出：
 *   返回 aux ring 中待发送元信息或故障帧数量。
 * 作用：
 *   仅在 UNIT_TEST 构建下提供只读观察口，用于验证 sample/aux 队列隔离。
 */
uint32_t usb_stream_test_get_aux_count(void)
{
  return g_usb_stream.aux_ring.count;
}
#endif
