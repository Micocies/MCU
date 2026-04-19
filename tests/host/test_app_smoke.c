#include "test_config.h"

#include "app.h"
#include "app_config.h"
#include "fake_hal.h"
#include "fake_usb.h"
#include "frame_protocol.h"
#include "project_config.h"
#include "test_assert.h"
#include "usb_stream.h"

static void reset_app_test(void)
{
  fake_hal_reset();
  fake_usb_reset();
  app_init();
}

static void drain_aux_queue(void)
{
  uint32_t guard = 16U;

  fake_usb_set_transmit_status(USBD_OK);
  while ((usb_stream_test_get_aux_count() != 0U) && (guard != 0U))
  {
    usb_stream_service();
    fake_usb_complete_tx();
    guard--;
  }
}

static void run_startup_to_scheduler(void)
{
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_BIAS_STABILIZE, app_test_get_state());

  fake_hal_advance_tick(APP_BIAS_STABILIZE_MS);
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_DRDY, app_test_get_state());
  TEST_ASSERT_EQ_U32(1U, fake_hal_get_tim_start_count());
}

static void arm_current_scheduler_slot(void)
{
  app_run_once();
  app_run_once();
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_DRDY, app_test_get_state());
}

static void run_one_scheduler_sample(int32_t raw_code)
{
  arm_current_scheduler_slot();

  fake_hal_queue_spi_receive_raw24(raw_code);
  app_on_drdy_isr();
  app_run_once();
  app_run_once();
  app_run_once();
  app_run_once();

}

static void test_startup_reaches_multi_device_scheduler(void)
{
  reset_app_test();
  run_startup_to_scheduler();

  TEST_ASSERT_EQ_U32(0U, app_test_get_fault_flags());
}

static void test_scheduler_builds_fixed_100_pixel_frame(void)
{
  const frame_packet_t *sent;
  uint16_t pixel_id;

  reset_app_test();
  run_startup_to_scheduler();
  drain_aux_queue();
  fake_usb_reset();

  for (pixel_id = 0U; pixel_id < PIXEL_COUNT; ++pixel_id)
  {
    if (pixel_id == (PIXEL_COUNT - 1U))
    {
      app_on_sample_tick_isr();
    }
    run_one_scheduler_sample((int32_t)(1000 + pixel_id));
    if (pixel_id < (PIXEL_COUNT - 1U))
    {
      TEST_ASSERT_EQ_INT(APP_STATE_WAIT_DRDY, app_test_get_state());
    }
  }

  TEST_ASSERT_EQ_INT(APP_STATE_USB_FLUSH, app_test_get_state());
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_DRDY, app_test_get_state());
  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());
  TEST_ASSERT_EQ_U32(sizeof(frame_packet_t), fake_usb_get_last_len());

  sent = fake_usb_get_last_frame();
  TEST_ASSERT_TRUE(frame_protocol_validate(sent));
  TEST_ASSERT_EQ_U32(FRAME_TYPE_FULL_REAL, sent->header.frame_type);
  TEST_ASSERT_EQ_U32(ARRAY_WIDTH, sent->header.width);
  TEST_ASSERT_EQ_U32(ARRAY_HEIGHT, sent->header.height);
  TEST_ASSERT_EQ_INT(1000, sent->pixels[0]);
  TEST_ASSERT_EQ_INT(1021, sent->pixels[21]);
  TEST_ASSERT_EQ_INT(1099, sent->pixels[99]);
}

static void test_drdy_timeout_enters_recover_and_resumes(void)
{
  reset_app_test();
  run_startup_to_scheduler();
  arm_current_scheduler_slot();

  fake_hal_advance_tick(APP_DRDY_TIMEOUT_MS);
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_RECOVER, app_test_get_state());
  TEST_ASSERT_TRUE((app_test_get_fault_flags() & SAMPLE_FLAG_DRDY_TIMEOUT) != 0U);

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_DRDY, app_test_get_state());
  TEST_ASSERT_EQ_U32(0U, app_test_get_fault_flags());
}

static void test_fault_reports_are_throttled(void)
{
  reset_app_test();
  fake_hal_set_dac_status(HAL_ERROR);

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_FAULT, app_test_get_state());

  fake_usb_complete_tx();
  drain_aux_queue();
  fake_usb_reset();
  app_run_once();
  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());

  fake_usb_complete_tx();
  app_run_once();
  TEST_ASSERT_EQ_U32(1U, fake_usb_get_transmit_count());

  fake_hal_advance_tick(APP_FAULT_REPORT_INTERVAL_MS);
  fake_usb_complete_tx();
  app_run_once();
  TEST_ASSERT_EQ_U32(2U, fake_usb_get_transmit_count());
}

void test_app_smoke_run(void)
{
  test_startup_reaches_multi_device_scheduler();
  test_scheduler_builds_fixed_100_pixel_frame();
  test_drdy_timeout_enters_recover_and_resumes();
  test_fault_reports_are_throttled();
}
