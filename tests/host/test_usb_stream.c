#include "test_config.h"

#include "app_config.h"
#include "fake_hal.h"
#include "fake_usb.h"
#include "frame_protocol.h"
#include "test_assert.h"
#include "usb_stream.h"

static sample_packet_t make_aux_packet(uint32_t sequence, int32_t raw_code, uint16_t flags)
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

static frame_packet_t make_frame(uint32_t frame_id, int32_t base_value)
{
  frame_packet_t frame;
  uint32_t i;

  frame_protocol_prepare_header(&frame.header, FRAME_TYPE_TEST, frame_id, frame_id * 10000U);
  for (i = 0U; i < PIXEL_COUNT; ++i)
  {
    frame.pixels[i] = base_value + (int32_t)i;
  }
  frame_protocol_finalize(&frame);
  return frame;
}

static void reset_stream_test(void)
{
  fake_hal_reset();
  fake_usb_reset();
  usb_stream_init();
}

static void test_init_is_empty_and_service_is_idle(void)
{
  reset_stream_test();

  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_frame_count());
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_aux_count());

  usb_stream_service();
  TEST_ASSERT_EQ_U32(0U, fake_usb_get_transmit_count());
}

static void test_frame_packet_flushes_as_420_bytes(void)
{
  frame_packet_t frame = make_frame(10U, 1000);
  const frame_packet_t *sent;

  reset_stream_test();

  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_frame(&frame));
  usb_stream_service();

  sent = fake_usb_get_last_frame();
  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(sizeof(frame_packet_t), fake_usb_get_last_len());
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_frame_count());
  TEST_ASSERT_EQ_U32(10U, sent->header.frame_id);
  TEST_ASSERT_EQ_INT(1000, sent->pixels[0]);
  TEST_ASSERT_TRUE(frame_protocol_validate(sent));
}

static void test_usb_busy_keeps_frame_for_retry(void)
{
  frame_packet_t frame = make_frame(3U, 300);
  usb_stream_stats_t stats;

  reset_stream_test();

  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_frame(&frame));
  fake_usb_set_transmit_status(USBD_BUSY);
  usb_stream_service();

  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(1U, usb_stream_test_get_frame_count());
  usb_stream_get_stats(&stats);
  TEST_ASSERT_EQ_U32(1U, stats.tx_busy);

  fake_usb_set_transmit_status(USBD_OK);
  usb_stream_service();

  TEST_ASSERT_EQ_U32(2U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_frame_count());
}

static void test_frame_overflow_records_stats_and_keeps_newest(void)
{
  frame_packet_t frame;
  usb_stream_stats_t stats;
  uint32_t i;

  reset_stream_test();

  for (i = 0U; i <= APP_USB_FRAME_QUEUE_DEPTH; ++i)
  {
    frame = make_frame(i, (int32_t)i);
    (void)usb_stream_enqueue_frame(&frame);
  }

  usb_stream_get_stats(&stats);
  TEST_ASSERT_EQ_U32(1U, stats.frame_overflow);
  TEST_ASSERT_EQ_U32(APP_USB_FRAME_QUEUE_DEPTH, usb_stream_test_get_frame_count());

  usb_stream_service();
  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(1U, fake_usb_get_last_frame()->header.frame_id);
}

static void test_aux_queue_stays_lower_priority_than_frame_queue(void)
{
  frame_packet_t frame = make_frame(42U, 420);
  sample_packet_t aux = make_aux_packet(7U, 700, SAMPLE_FLAG_INFO_FRAME);
  const sample_packet_t *sent_aux;

  reset_stream_test();

  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_frame(&frame));
  TEST_ASSERT_EQ_INT(USB_STREAM_ENQUEUE_OK, usb_stream_enqueue_aux(&aux));

  usb_stream_service();
  TEST_ASSERT_EQ_U32(sizeof(frame_packet_t), fake_usb_get_last_len());
  TEST_ASSERT_EQ_U32(42U, fake_usb_get_last_frame()->header.frame_id);
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_frame_count());
  TEST_ASSERT_EQ_U32(1U, usb_stream_test_get_aux_count());

  usb_stream_service();
  sent_aux = fake_usb_get_last_packets();
  TEST_ASSERT_EQ_U32(sizeof(sample_packet_t), fake_usb_get_last_len());
  TEST_ASSERT_EQ_U32(7U, sent_aux[0].sequence);
  TEST_ASSERT_TRUE((sent_aux[0].flags & SAMPLE_FLAG_INFO_FRAME) != 0U);
  TEST_ASSERT_EQ_U32(0U, usb_stream_test_get_aux_count());
}

void test_usb_stream_run(void)
{
  test_init_is_empty_and_service_is_idle();
  test_frame_packet_flushes_as_420_bytes();
  test_usb_busy_keeps_frame_for_retry();
  test_frame_overflow_records_stats_and_keeps_newest();
  test_aux_queue_stays_lower_priority_than_frame_queue();
}
