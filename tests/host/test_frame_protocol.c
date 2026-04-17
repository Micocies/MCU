#include "test_config.h"

#include "frame_builder.h"
#include "frame_protocol.h"
#include "pixel_map.h"
#include "test_assert.h"

static void test_frame_header_and_crc_are_valid(void)
{
  frame_packet_t frame;
  uint32_t i;

  frame_protocol_prepare_header(&frame.header, FRAME_TYPE_TEST, 123U, 456U);
  for (i = 0U; i < PIXEL_COUNT; ++i)
  {
    frame.pixels[i] = (int32_t)i;
  }
  frame_protocol_finalize(&frame);

  TEST_ASSERT_EQ_U32(FRAME_PROTOCOL_HEADER_BYTES + FRAME_PROTOCOL_PAYLOAD_BYTES, sizeof(frame_packet_t));
  TEST_ASSERT_EQ_U32(FRAME_PROTOCOL_MAGIC, frame.header.magic);
  TEST_ASSERT_EQ_U32(FRAME_PROTOCOL_VERSION, frame.header.version);
  TEST_ASSERT_EQ_U32(ARRAY_WIDTH, frame.header.width);
  TEST_ASSERT_EQ_U32(ARRAY_HEIGHT, frame.header.height);
  TEST_ASSERT_EQ_U32(FRAME_PROTOCOL_PAYLOAD_BYTES, frame.header.payload_bytes);
  TEST_ASSERT_TRUE(frame_protocol_validate(&frame));

  frame.pixels[99] += 1;
  TEST_ASSERT_TRUE(!frame_protocol_validate(&frame));
}

static void test_pixel_map_is_row_major_and_fixed_to_100_pixels(void)
{
  pixel_map_entry_t entry;

  TEST_ASSERT_EQ_U32(0U, pixel_map_make_id(0U, 0U));
  TEST_ASSERT_EQ_U32(9U, pixel_map_make_id(0U, 9U));
  TEST_ASSERT_EQ_U32(10U, pixel_map_make_id(1U, 0U));
  TEST_ASSERT_EQ_U32(99U, pixel_map_make_id(9U, 9U));
  TEST_ASSERT_EQ_U32(PIXEL_COUNT, pixel_map_make_id(10U, 0U));

  TEST_ASSERT_TRUE(pixel_map_get_entry(23U, &entry));
  TEST_ASSERT_EQ_U32(23U, entry.pixel_id);
  TEST_ASSERT_EQ_U32(2U, entry.row);
  TEST_ASSERT_EQ_U32(3U, entry.column);
  TEST_ASSERT_EQ_U32(1U, entry.subboard_id);
  TEST_ASSERT_EQ_U32(0U, entry.adc_id);
  TEST_ASSERT_EQ_U32(3U, entry.adc_channel);
}

static void test_frame_builder_partial_real_places_sample_at_active_pixel(void)
{
  frame_builder_t builder;
  frame_packet_t frame;

  frame_builder_init(&builder, FRAME_TYPE_PARTIAL_REAL);
  frame_builder_build(&builder, &frame, 5U, 50000U, -1234);

  TEST_ASSERT_TRUE(frame_protocol_validate(&frame));
  TEST_ASSERT_EQ_U32(FRAME_TYPE_PARTIAL_REAL, frame.header.frame_type);
  TEST_ASSERT_EQ_INT(-1234, frame.pixels[PROJECT_ACTIVE_PIXEL_ID]);
  TEST_ASSERT_EQ_INT(10 + 5, frame.pixels[1]);
  TEST_ASSERT_EQ_INT(9000 + 90 + 5, frame.pixels[99]);
}

void test_frame_protocol_run(void)
{
  test_frame_header_and_crc_are_valid();
  test_pixel_map_is_row_major_and_fixed_to_100_pixels();
  test_frame_builder_partial_real_places_sample_at_active_pixel();
}
