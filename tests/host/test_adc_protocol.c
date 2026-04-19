#include "test_config.h"

#include "adc_bus.h"
#include "adc_protocol.h"
#include "app_config.h"
#include "fake_hal.h"
#include "test_assert.h"

/* 函数说明：
 *   验证 ADS1220 24 bit 补码解析边界。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   覆盖最大正数、最小负数、-1 和 0，防止符号扩展退化。
 */
static void test_parse_raw24_boundaries(void)
{
  const uint8_t max_positive[ADS1220_DATA_BYTES] = {0x7FU, 0xFFU, 0xFFU};
  const uint8_t min_negative[ADS1220_DATA_BYTES] = {0x80U, 0x00U, 0x00U};
  const uint8_t minus_one[ADS1220_DATA_BYTES] = {0xFFU, 0xFFU, 0xFFU};
  const uint8_t zero[ADS1220_DATA_BYTES] = {0x00U, 0x00U, 0x00U};

  TEST_ASSERT_EQ_INT(8388607, adc_protocol_parse_raw24(max_positive));
  TEST_ASSERT_EQ_INT(-8388608, adc_protocol_parse_raw24(min_negative));
  TEST_ASSERT_EQ_INT(-1, adc_protocol_parse_raw24(minus_one));
  TEST_ASSERT_EQ_INT(0, adc_protocol_parse_raw24(zero));
}

/* 函数说明：
 *   验证 ADC 码值到电压的换算边界。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   确认 gain 为 0 时安全返回，并检查正常参数下的方向和数量级。
 */
static void test_code_to_voltage_boundaries(void)
{
  float voltage;

  TEST_ASSERT_EQ_FLOAT(0.0f, adc_protocol_code_to_voltage(1234, 2.048f, 0.0f), 0.000001f);

  voltage = adc_protocol_code_to_voltage(4194304, 2.048f, 1.0f);
  TEST_ASSERT_TRUE(voltage > 1.02f);
  TEST_ASSERT_TRUE(voltage < 1.03f);
  TEST_ASSERT_TRUE(adc_protocol_code_to_voltage(-4194304, 2.048f, 1.0f) < 0.0f);
}

/* 函数说明：
 *   验证 HAL SPI 错误到协议层错误码的映射。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   使用 fake HAL 返回 timeout/error，确认上层能区分超时和普通 SPI 错误。
 */
static void test_spi_error_mapping(void)
{
  SPI_HandleTypeDef spi;
  adc_bus_t bus;

  fake_hal_reset();
  adc_bus_init(&bus, &spi);
  adc_protocol_init(&bus);

  fake_hal_set_spi_status(HAL_TIMEOUT);
  TEST_ASSERT_EQ_INT(ADC_PROTOCOL_ERR_TIMEOUT, adc_protocol_send_command(ADS1220_CMD_RESET));

  fake_hal_set_spi_status(HAL_ERROR);
  TEST_ASSERT_EQ_INT(ADC_PROTOCOL_ERR_SPI, adc_protocol_send_command(ADS1220_CMD_RESET));
}

/* 函数说明：
 *   验证 ADS1220 配置写寄存器的发送序列。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   通过 fake SPI 记录最后一次 TX 缓冲区，确认 WREG 命令和 4 个寄存器按预期发送。
 */
static void test_configure_writes_expected_sequence(void)
{
  SPI_HandleTypeDef spi;
  adc_bus_t bus;
  ads1220_config_t config = {{0x11U, 0x22U, 0x33U, 0x44U}};
  const uint8_t *tx;

  fake_hal_reset();
  adc_bus_init(&bus, &spi);
  adc_protocol_init(&bus);
  fake_hal_set_spi_status(HAL_OK);

  TEST_ASSERT_EQ_INT(ADC_PROTOCOL_OK, adc_protocol_configure(&config));
  tx = fake_hal_get_last_spi_tx();

  TEST_ASSERT_EQ_U32(5U, fake_hal_get_last_spi_size());
  TEST_ASSERT_EQ_U32(ADS1220_CMD_WREG(ADS1220_REG_CONFIG0, ADS1220_REG_COUNT), tx[0]);
  TEST_ASSERT_EQ_U32(0x11U, tx[1]);
  TEST_ASSERT_EQ_U32(0x22U, tx[2]);
  TEST_ASSERT_EQ_U32(0x33U, tx[3]);
  TEST_ASSERT_EQ_U32(0x44U, tx[4]);
}

static void test_link_check_records_mismatch_retries(void)
{
  SPI_HandleTypeDef spi;
  adc_bus_t bus;
  ads1220_config_t config = {{
    ADS1220_DEFAULT_CONFIG0,
    ADS1220_DEFAULT_CONFIG1,
    ADS1220_DEFAULT_CONFIG2,
    ADS1220_DEFAULT_CONFIG3
  }};
  adc_protocol_link_stats_t stats;

  fake_hal_reset();
  adc_bus_init(&bus, &spi);
  adc_protocol_init(&bus);
  fake_hal_set_config_mismatch(1U);

  TEST_ASSERT_EQ_INT(ADC_PROTOCOL_ERR_CONFIG_MISMATCH, adc_protocol_link_check(&config));
  adc_protocol_get_link_stats(&stats);

  TEST_ASSERT_EQ_U32(1U, stats.total_checks);
  TEST_ASSERT_EQ_U32(APP_ADC_LINK_CHECK_RETRIES, stats.retries);
  TEST_ASSERT_EQ_U32(APP_ADC_LINK_CHECK_RETRIES + 1U, stats.mismatches);
  TEST_ASSERT_EQ_INT(ADC_PROTOCOL_ERR_CONFIG_MISMATCH, stats.last_status);
}

/* 函数说明：
 *   执行 adc_protocol 测试组。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   由 host 测试主入口顺序调用，聚合协议层最小单元测试。
 */
void test_adc_protocol_run(void)
{
  test_parse_raw24_boundaries();
  test_code_to_voltage_boundaries();
  test_spi_error_mapping();
  test_configure_writes_expected_sequence();
  test_link_check_records_mismatch_retries();
}
