#include "test_config.h"

#include "app.h"
#include "app_config.h"
#include "diag.h"
#include "fake_hal.h"
#include "fake_usb.h"
#include "test_assert.h"
#include "usb_stream.h"

/* 函数说明：
 *   重置 app smoke 测试上下文。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   清空 fake 外设状态后调用 app_init，保证状态机从上电入口开始。
 */
static void reset_app_test(void)
{
  fake_hal_reset();
  fake_usb_reset();
  app_init();
}

static void complete_current_conversion(int32_t raw_code);

/* 函数说明：
 *   推动启动流程到通信自检等待 DRDY。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   复用 INIT、BIAS_STABILIZE、COMM_CHECK 的固定推进步骤，减少用例重复。
 */
static void run_startup_to_comm_wait(void)
{
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_BIAS_STABILIZE, app_test_get_state());

  fake_hal_advance_tick(APP_BIAS_STABILIZE_MS);
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_COMM_CHECK, app_test_get_state());

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_DRDY, app_test_get_state());
}

static void run_recovered_startup_to_dark_calibration(void)
{
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_BIAS_STABILIZE, app_test_get_state());

  fake_hal_advance_tick(APP_BIAS_STABILIZE_MS);
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_COMM_CHECK, app_test_get_state());

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_DRDY, app_test_get_state());
  complete_current_conversion(1000);
  TEST_ASSERT_EQ_INT(APP_STATE_DARK_CALIBRATE, app_test_get_state());
}

/* 函数说明：
 *   完成当前一次 ADC 转换。
 * 输入：
 *   raw_code: fake SPI 返回的原始 ADC 码值。
 * 输出：
 *   无。
 * 作用：
 *   注入一个样本并触发 DRDY，驱动状态机走完 READ_SAMPLE 和 PROCESS_SAMPLE。
 */
static void complete_current_conversion(int32_t raw_code)
{
  fake_hal_queue_spi_receive_raw24(raw_code);
  app_on_drdy_isr();
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_READ_SAMPLE, app_test_get_state());

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_PROCESS_SAMPLE, app_test_get_state());

  app_run_once();
}

/* 函数说明：
 *   运行一次由 TIM6 节拍触发的采样。
 * 输入：
 *   raw_code: fake SPI 返回的原始 ADC 码值。
 * 输出：
 *   无。
 * 作用：
 *   模拟主循环收到 sample tick 后发起转换，并复用 DRDY 完成路径。
 */
static void run_one_triggered_sample(int32_t raw_code)
{
  app_on_sample_tick_isr();
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_DRDY, app_test_get_state());
  complete_current_conversion(raw_code);
}

/* 函数说明：
 *   完成暗态校准所需的固定样本数。
 * 输入：
 *   raw_code: 每个校准样本使用的 fake ADC 码值。
 * 输出：
 *   无。
 * 作用：
 *   以可控样本推动校准闭环，便于断言最终 baseline_code。
 */
static void run_calibration_samples(int32_t raw_code)
{
  uint32_t i;

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_TRIGGER, app_test_get_state());

  for (i = 0U; i < APP_DARK_CALIBRATION_SAMPLES; ++i)
  {
    run_one_triggered_sample(raw_code);
    TEST_ASSERT_EQ_INT(APP_STATE_WAIT_TRIGGER, app_test_get_state());
  }
}

/* 函数说明：
 *   验证启动后可进入暗态校准路径。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   覆盖上电初始化、偏置等待、通信自检成功后的关键状态迁移。
 */
static void test_startup_reaches_dark_calibration_without_fault(void)
{
  reset_app_test();
  run_startup_to_comm_wait();
  complete_current_conversion(1000);

  TEST_ASSERT_EQ_INT(APP_STATE_DARK_CALIBRATE, app_test_get_state());
  TEST_ASSERT_EQ_U32(0U, app_test_get_fault_flags());
}

/* 函数说明：
 *   验证 DRDY 超时故障路径。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   在通信自检等待 DRDY 时推进 fake tick，确认进入 FAULT 并设置对应标志。
 */
static void test_drdy_timeout_recovers_by_reconfigure(void)
{
  reset_app_test();
  run_startup_to_comm_wait();

  fake_hal_advance_tick(APP_DRDY_TIMEOUT_MS);
  app_run_once();

  TEST_ASSERT_EQ_INT(APP_STATE_RECOVER, app_test_get_state());
  TEST_ASSERT_TRUE((app_test_get_fault_flags() & SAMPLE_FLAG_DRDY_TIMEOUT) != 0U);
  TEST_ASSERT_TRUE((app_test_get_fault_flags() & SAMPLE_FLAG_COMM_CHECK_FAILED) != 0U);
  TEST_ASSERT_EQ_U32(1U, diag_get_fault_count(DIAG_FAULT_DRDY_TIMEOUT));

  run_recovered_startup_to_dark_calibration();
  TEST_ASSERT_EQ_U32(0U, app_test_get_fault_flags());
}

static void test_spi_timeout_recovers_by_retry(void)
{
  reset_app_test();
  run_startup_to_comm_wait();

  fake_hal_queue_spi_receive_raw24(1000);
  app_on_drdy_isr();
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_READ_SAMPLE, app_test_get_state());

  fake_hal_set_spi_receive_status(HAL_TIMEOUT);
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_RECOVER, app_test_get_state());
  TEST_ASSERT_TRUE((app_test_get_fault_flags() & SAMPLE_FLAG_SPI_TIMEOUT) != 0U);
  TEST_ASSERT_EQ_U32(1U, diag_get_fault_count(DIAG_FAULT_SPI_TIMEOUT));

  fake_hal_set_spi_receive_status(HAL_OK);
  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_DRDY, app_test_get_state());

  complete_current_conversion(1000);
  TEST_ASSERT_EQ_INT(APP_STATE_DARK_CALIBRATE, app_test_get_state());
  TEST_ASSERT_EQ_U32(0U, app_test_get_fault_flags());
}

static void test_config_mismatch_recovers_by_reconfigure(void)
{
  reset_app_test();
  run_startup_to_comm_wait();

  fake_hal_set_config_mismatch(1U);
  complete_current_conversion(1000);
  TEST_ASSERT_EQ_INT(APP_STATE_RECOVER, app_test_get_state());
  TEST_ASSERT_TRUE((app_test_get_fault_flags() & SAMPLE_FLAG_CONFIG_MISMATCH) != 0U);
  TEST_ASSERT_EQ_U32(1U, diag_get_fault_count(DIAG_FAULT_CONFIG_MISMATCH));

  fake_hal_set_config_mismatch(0U);
  run_recovered_startup_to_dark_calibration();
  TEST_ASSERT_EQ_U32(0U, app_test_get_fault_flags());
}

static void test_repeated_recovery_failure_enters_fault_hold(void)
{
  reset_app_test();
  run_startup_to_comm_wait();

  fake_hal_set_config_mismatch(1U);
  complete_current_conversion(1000);
  TEST_ASSERT_EQ_INT(APP_STATE_RECOVER, app_test_get_state());

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_RECOVER, app_test_get_state());

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_RECOVER, app_test_get_state());

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_FAULT, app_test_get_state());
  TEST_ASSERT_TRUE((app_test_get_fault_flags() & SAMPLE_FLAG_RECOVERY_FAILED) != 0U);
  TEST_ASSERT_EQ_U32(4U, diag_get_fault_count(DIAG_FAULT_CONFIG_MISMATCH));
}

/* 函数说明：
 *   验证校准完成后进入运行态并产生样本。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   使用固定 fake ADC 样本完成暗态校准，再确认运行样本被压入 USB sample 队列。
 */
static void test_calibration_then_run_enqueues_sample(void)
{
  reset_app_test();
  run_startup_to_comm_wait();
  complete_current_conversion(1000);
  TEST_ASSERT_EQ_INT(APP_STATE_DARK_CALIBRATE, app_test_get_state());

  run_calibration_samples(1000);
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_TRIGGER, app_test_get_state());
  TEST_ASSERT_EQ_INT(1000, app_test_get_baseline_code());
  TEST_ASSERT_EQ_U32(0U, app_test_get_fault_flags());

  run_one_triggered_sample(1100);
  TEST_ASSERT_EQ_INT(APP_STATE_USB_FLUSH, app_test_get_state());

  app_run_once();
  TEST_ASSERT_EQ_INT(APP_STATE_WAIT_TRIGGER, app_test_get_state());
  TEST_ASSERT_EQ_U32(1U, usb_stream_test_get_sample_count());
}

/* 函数说明：
 *   执行 app smoke 测试组。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   由 host 测试主入口顺序调用，覆盖状态机最小启动、故障和运行路径。
 */
void test_app_smoke_run(void)
{
  test_startup_reaches_dark_calibration_without_fault();
  test_drdy_timeout_recovers_by_reconfigure();
  test_spi_timeout_recovers_by_retry();
  test_config_mismatch_recovers_by_reconfigure();
  test_repeated_recovery_failure_enters_fault_hold();
  test_calibration_then_run_enqueues_sample();
}
