#include "test_config.h"

#include "app_config.h"
#include "fake_hal.h"
#include "fake_usb.h"
#include "test_assert.h"
#include "usb_stream.h"

/* 函数说明：
 *   构造一个固定 32 字节样本帧。
 * 输入：
 *   sequence: 样本或辅助帧序号。
 *   raw_code: 原始 ADC 码值。
 *   flags: 帧标志位。
 * 输出：
 *   返回填好的 sample_packet_t。
 * 作用：
 *   避免每个测试重复手写固定包头和负载字段。
 */
static sample_packet_t make_packet(uint32_t sequence, int32_t raw_code, uint16_t flags)
{
  sample_packet_t pkt;

  pkt.magic = SAMPLE_PACKET_MAGIC;
  pkt.version = SAMPLE_PACKET_VERSION;
  pkt.state = 0U;
  pkt.flags = flags;
  pkt.reserved = 0U;
  pkt.sequence = sequence;
  pkt.timestamp_us = sequence * 1000U;
  pkt.raw_code = raw_code;
  pkt.filtered_code = raw_code;
  pkt.baseline_code = 0;
  pkt.corrected_code = raw_code;
  return pkt;
}

/* 函数说明：
 *   重置 USB stream 测试上下文。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   清空 fake HAL tick、fake USB 记录和被测队列，保证每个用例互不污染。
 */
static void reset_stream_test(void)
{
  fake_hal_reset();
  fake_usb_reset();
  usb_stream_init();
}

/* 函数说明：
 *   验证初始化后的空队列行为。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   确认 service 在无待发数据时不会调用 CDC 发送。
 */
static void test_init_is_empty_and_service_is_idle(void)
{
  reset_stream_test();

  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_sample_count());
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_aux_count());

  usb_stream_service();
  TEST_ASSERT_EQ_U32(0U, fake_usb_get_transmit_count());
}

/* 函数说明：
 *   验证单包延迟发送策略。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   通过 fake tick 推进等待窗口，确认单个样本不会立即发送，超时后以 32 byte 发出。
 */
static void test_single_packet_waits_then_flushes(void)
{
  sample_packet_t pkt = make_packet(10U, 123, 0U);
  const sample_packet_t *sent;

  reset_stream_test();
  fake_hal_set_tick(100U);

  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_sample(&pkt));
  usb_stream_service();
  TEST_ASSERT_EQ_U32(0U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(1U, usb_stream_test_get_sample_count());

  fake_hal_advance_tick(APP_USB_BATCH_MAX_WAIT_MS);
  usb_stream_service();

  sent = fake_usb_get_last_packets();
  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(sizeof(sample_packet_t), fake_usb_get_last_len());
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_sample_count());
  TEST_ASSERT_EQ_U32(10U, sent[0].sequence);
  TEST_ASSERT_EQ_INT(123, sent[0].raw_code);
}

/* 函数说明：
 *   验证两个样本帧的聚合发送策略。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   确认 sample 队列中有两个 32 byte 帧时优先合并为一次 64 byte CDC 发送。
 */
static void test_two_sample_packets_batch_to_64_bytes(void)
{
  sample_packet_t pkt0 = make_packet(1U, 100, 0U);
  sample_packet_t pkt1 = make_packet(2U, 200, 0U);
  const sample_packet_t *sent;

  reset_stream_test();

  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_sample(&pkt0));
  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_sample(&pkt1));
  usb_stream_service();

  sent = fake_usb_get_last_packets();
  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(2U * sizeof(sample_packet_t), fake_usb_get_last_len());
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_sample_count());
  TEST_ASSERT_EQ_U32(1U, sent[0].sequence);
  TEST_ASSERT_EQ_U32(2U, sent[1].sequence);
}

/* 函数说明：
 *   验证 USB busy 时队列保留策略。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   fake CDC 返回 busy 后不弹出样本，下一轮恢复 OK 后可重试成功。
 */
static void test_usb_busy_keeps_packet_for_retry(void)
{
  sample_packet_t pkt = make_packet(3U, 300, 0U);

  reset_stream_test();

  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_sample(&pkt));
  fake_hal_advance_tick(APP_USB_BATCH_MAX_WAIT_MS);
  fake_usb_set_transmit_status(USBD_BUSY);
  usb_stream_service();

  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(1U, usb_stream_test_get_sample_count());

  fake_usb_set_transmit_status(USBD_OK);
  usb_stream_service();

  TEST_ASSERT_EQ_U32(2U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_sample_count());
}

/* 函数说明：
 *   验证样本队列和辅助队列隔离。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   确认 sample 优先级和 sequence 不受 metadata/aux 帧影响。
 */
static void test_aux_queue_does_not_break_sample_sequence(void)
{
  sample_packet_t sample = make_packet(42U, 420, 0U);
  sample_packet_t aux = make_packet(7U, 700, SAMPLE_FLAG_INFO_FRAME);
  const sample_packet_t *sent;

  reset_stream_test();

  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_sample(&sample));
  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_aux(&aux));

  usb_stream_service();
  TEST_ASSERT_EQ_U32(0U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(1U, usb_stream_test_get_sample_count());
  TEST_ASSERT_EQ_U32(1U, usb_stream_test_get_aux_count());

  fake_hal_advance_tick(APP_USB_BATCH_MAX_WAIT_MS);
  usb_stream_service();
  sent = fake_usb_get_last_packets();
  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(42U, sent[0].sequence);
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_sample_count());
  TEST_ASSERT_EQ_U32(1U, usb_stream_test_get_aux_count());

  usb_stream_service();
  sent = fake_usb_get_last_packets();
  TEST_ASSERT_EQ_U32(2U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(7U, sent[0].sequence);
  TEST_ASSERT_TRUE((sent[0].flags & SAMPLE_FLAG_INFO_FRAME) != 0U);
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_aux_count());
}

/* 函数说明：
 *   执行 usb_stream 测试组。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   由 host 测试主入口顺序调用，聚合 USB 队列与发送策略测试。
 */
void test_usb_stream_run(void)
{
  test_init_is_empty_and_service_is_idle();
  test_single_packet_waits_then_flushes();
  test_two_sample_packets_batch_to_64_bytes();
  test_usb_busy_keeps_packet_for_retry();
  test_aux_queue_does_not_break_sample_sequence();
}
