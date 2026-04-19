#include "adc_protocol.h"

#include <string.h>

#include "app_config.h"

static adc_bus_t *g_bus;
static adc_protocol_link_stats_t g_link_stats;

static ads1220_config_t adc_protocol_default_config(void)
{
  ads1220_config_t config = {
    {
      ADS1220_DEFAULT_CONFIG0,
      ADS1220_DEFAULT_CONFIG1,
      ADS1220_DEFAULT_CONFIG2,
      ADS1220_DEFAULT_CONFIG3
    }
  };

  return config;
}

static void adc_protocol_delay_cycles(uint32_t cycles)
{
  uint32_t i;

  for (i = 0U; i < cycles; ++i)
  {
    __NOP();
  }
}

static adc_protocol_status_t adc_protocol_status_from_hal(HAL_StatusTypeDef hal_status)
{
  if (hal_status == HAL_OK)
  {
    return ADC_PROTOCOL_OK;
  }

  if (hal_status == HAL_TIMEOUT)
  {
    return ADC_PROTOCOL_ERR_TIMEOUT;
  }

  return ADC_PROTOCOL_ERR_SPI;
}

static adc_protocol_status_t adc_protocol_transfer(const uint8_t *tx_buf,
                                                   uint8_t *rx_buf,
                                                   uint16_t size,
                                                   uint32_t timeout_ms)
{
  HAL_StatusTypeDef hal_status;

  if ((g_bus == 0) || (size == 0U))
  {
    return (g_bus == 0) ? ADC_PROTOCOL_ERR_NOT_INIT : ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  hal_status = adc_bus_txrx(tx_buf, rx_buf, size, timeout_ms);
  return adc_protocol_status_from_hal(hal_status);
}

void adc_protocol_init(adc_bus_t *bus)
{
  g_bus = bus;
  adc_protocol_reset_link_stats();
  adc_bus_cs_deassert();
}

adc_protocol_status_t adc_protocol_send_command(uint8_t command)
{
  uint8_t rx_dummy = 0U;
  adc_protocol_status_t status;

  adc_bus_cs_assert();
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  status = adc_protocol_transfer(&command, &rx_dummy, 1U, APP_ADC_SPI_TIMEOUT_INIT_MS);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_bus_cs_deassert();

  return status;
}

adc_protocol_status_t adc_protocol_configure(const ads1220_config_t *config)
{
  uint8_t tx_buf[1U + ADS1220_REG_COUNT] = {0U};
  uint8_t rx_buf[1U + ADS1220_REG_COUNT] = {0U};
  adc_protocol_status_t status;

  if ((g_bus == 0) || (config == 0))
  {
    return (g_bus == 0) ? ADC_PROTOCOL_ERR_NOT_INIT : ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  tx_buf[0] = ADS1220_CMD_WREG(ADS1220_REG_CONFIG0, ADS1220_REG_COUNT);
  tx_buf[1] = config->reg[ADS1220_REG_CONFIG0];
  tx_buf[2] = config->reg[ADS1220_REG_CONFIG1];
  tx_buf[3] = config->reg[ADS1220_REG_CONFIG2];
  tx_buf[4] = config->reg[ADS1220_REG_CONFIG3];

  adc_bus_cs_assert();
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  status = adc_protocol_transfer(tx_buf, rx_buf, (uint16_t)sizeof(tx_buf), APP_ADC_SPI_TIMEOUT_INIT_MS);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_bus_cs_deassert();

  return status;
}

adc_protocol_status_t adc_protocol_configure_default(void)
{
  ads1220_config_t config = adc_protocol_default_config();

  return adc_protocol_configure(&config);
}

adc_protocol_status_t adc_protocol_read_config(ads1220_config_t *config)
{
  uint8_t tx_buf[1U + ADS1220_REG_COUNT] = {0U};
  uint8_t rx_buf[1U + ADS1220_REG_COUNT] = {0U};
  adc_protocol_status_t status;

  if ((g_bus == 0) || (config == 0))
  {
    return (g_bus == 0) ? ADC_PROTOCOL_ERR_NOT_INIT : ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  tx_buf[0] = ADS1220_CMD_RREG(ADS1220_REG_CONFIG0, ADS1220_REG_COUNT);

  adc_bus_cs_assert();
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  status = adc_protocol_transfer(tx_buf, rx_buf, (uint16_t)sizeof(tx_buf), APP_ADC_SPI_TIMEOUT_INIT_MS);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_bus_cs_deassert();
  if (status != ADC_PROTOCOL_OK)
  {
    return status;
  }

  config->reg[ADS1220_REG_CONFIG0] = rx_buf[1];
  config->reg[ADS1220_REG_CONFIG1] = rx_buf[2];
  config->reg[ADS1220_REG_CONFIG2] = rx_buf[3];
  config->reg[ADS1220_REG_CONFIG3] = rx_buf[4];
  return ADC_PROTOCOL_OK;
}

adc_protocol_status_t adc_protocol_reset(void)
{
  adc_protocol_status_t status;

  status = adc_protocol_send_command(ADS1220_CMD_RESET);
  HAL_Delay(APP_ADC_RESET_PULSE_MS);
  return status;
}

adc_protocol_status_t adc_protocol_stop(void)
{
  return adc_protocol_send_command(ADS1220_CMD_POWERDOWN);
}

bool adc_protocol_is_continuous_config(const ads1220_config_t *config)
{
  uint8_t config1;

  if (config == 0)
  {
    return false;
  }

  config1 = config->reg[ADS1220_REG_CONFIG1];
  return ((config1 & ADS1220_CONFIG1_DR_MASK) == ADS1220_CONFIG1_DR_2000SPS) &&
         ((config1 & ADS1220_CONFIG1_MODE_MASK) == ADS1220_CONFIG1_MODE_TURBO) &&
         ((config1 & ADS1220_CONFIG1_CM_MASK) == ADS1220_CONFIG1_CM_CONTINUOUS);
}

adc_protocol_status_t adc_protocol_start_continuous(const ads1220_config_t *expected_config)
{
  if (!adc_protocol_is_continuous_config(expected_config))
  {
    return ADC_PROTOCOL_ERR_CONFIG_MISMATCH;
  }

  return adc_protocol_send_command(ADS1220_CMD_START_SYNC);
}

adc_protocol_status_t adc_protocol_stop_continuous(void)
{
  return adc_protocol_stop();
}

adc_protocol_status_t adc_protocol_start_conversion(void)
{
  ads1220_config_t config = adc_protocol_default_config();

  return adc_protocol_start_continuous(&config);
}

adc_protocol_status_t adc_protocol_read_raw24(uint8_t data[ADS1220_DATA_BYTES])
{
  HAL_StatusTypeDef hal_status;

  if ((g_bus == 0) || (data == 0))
  {
    return (g_bus == 0) ? ADC_PROTOCOL_ERR_NOT_INIT : ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  adc_bus_cs_assert();
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  hal_status = adc_bus_rx(data, ADS1220_DATA_BYTES, APP_ADC_SPI_TIMEOUT_RUN_MS);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_bus_cs_deassert();
  if (hal_status != HAL_OK)
  {
    return adc_protocol_status_from_hal(hal_status);
  }

  return ADC_PROTOCOL_OK;
}

int32_t adc_protocol_parse_raw24(const uint8_t data[ADS1220_DATA_BYTES])
{
  int32_t code;

  code = ((int32_t)data[0] << 16)
       | ((int32_t)data[1] << 8)
       | (int32_t)data[2];

  if ((code & 0x00800000L) != 0)
  {
    code |= (int32_t)0xFF000000L;
  }

  return code;
}

float adc_protocol_code_to_voltage(int32_t code, float vref, float gain)
{
  if (gain == 0.0f)
  {
    return 0.0f;
  }

  return ((float)code) * ((2.0f * vref) / gain) / 16777216.0f;
}

adc_protocol_status_t adc_protocol_read_sample(int32_t *raw_code)
{
  uint8_t data[ADS1220_DATA_BYTES] = {0U};
  adc_protocol_status_t status;

  if (raw_code == 0)
  {
    return ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  status = adc_protocol_read_raw24(data);
  if (status != ADC_PROTOCOL_OK)
  {
    return status;
  }

  *raw_code = adc_protocol_parse_raw24(data);
  return ADC_PROTOCOL_OK;
}

adc_protocol_status_t adc_protocol_link_check(const ads1220_config_t *expected_config)
{
  ads1220_config_t config;
  adc_protocol_status_t status;
  uint32_t attempt;

  if (expected_config == 0)
  {
    return ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  g_link_stats.total_checks++;
  g_link_stats.expected_config = *expected_config;

  for (attempt = 0U; attempt <= APP_ADC_LINK_CHECK_RETRIES; ++attempt)
  {
    status = adc_protocol_read_config(&config);
    g_link_stats.last_status = status;
    if (status != ADC_PROTOCOL_OK)
    {
      g_link_stats.read_errors++;
      if (attempt < APP_ADC_LINK_CHECK_RETRIES)
      {
        g_link_stats.retries++;
        continue;
      }
      return status;
    }

    g_link_stats.last_read_config = config;
    if (memcmp(&config, expected_config, sizeof(config)) != 0)
    {
      g_link_stats.mismatches++;
      g_link_stats.last_status = ADC_PROTOCOL_ERR_CONFIG_MISMATCH;
      if (attempt < APP_ADC_LINK_CHECK_RETRIES)
      {
        g_link_stats.retries++;
        continue;
      }
      return ADC_PROTOCOL_ERR_CONFIG_MISMATCH;
    }

    g_link_stats.successful_checks++;
    g_link_stats.last_status = ADC_PROTOCOL_OK;
    return ADC_PROTOCOL_OK;
  }

  g_link_stats.last_status = ADC_PROTOCOL_ERR_CONFIG_MISMATCH;
  return ADC_PROTOCOL_ERR_CONFIG_MISMATCH;
}

void adc_protocol_get_link_stats(adc_protocol_link_stats_t *stats)
{
  if (stats == 0)
  {
    return;
  }

  *stats = g_link_stats;
}

void adc_protocol_reset_link_stats(void)
{
  memset(&g_link_stats, 0, sizeof(g_link_stats));
}
