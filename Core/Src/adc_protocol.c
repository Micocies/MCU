#include "adc_protocol.h"

#include <string.h>

#include "app_config.h"

static SPI_HandleTypeDef *g_spi = NULL;
/* 保存期望寄存器镜像。
 * COMM_CHECK 阶段通过回读寄存器来确认器件配置没有写偏。 */
static ads1220_config_t g_expected_config = {
  {
    ADS1220_DEFAULT_CONFIG0,
    ADS1220_DEFAULT_CONFIG1,
    ADS1220_DEFAULT_CONFIG2,
    ADS1220_DEFAULT_CONFIG3
  }
};

static void adc_protocol_chip_select(GPIO_PinState state)
{
  HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, state);
}

/* 某些命令和读写动作前后插入少量空周期，给 SPI 事务留出边界。 */
static void adc_protocol_delay_cycles(uint32_t cycles)
{
  uint32_t i;

  for (i = 0U; i < cycles; ++i)
  {
    __NOP();
  }
}

/* 统一的全双工传输封装。
 * ADS1220 的命令、寄存器读写都通过这里发送。 */
static bool adc_protocol_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t size)
{
  if ((g_spi == NULL) || (size == 0U))
  {
    return false;
  }

  if (HAL_SPI_TransmitReceive(g_spi, (uint8_t *)tx_buf, rx_buf, size, HAL_MAX_DELAY) != HAL_OK)
  {
    return false;
  }

  return true;
}

void adc_protocol_init(SPI_HandleTypeDef *hspi)
{
  g_spi = hspi;
  adc_protocol_chip_select(GPIO_PIN_SET);
  /* START 和 RST 在空闲时保持低/高，避免上电时误触发。 */
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ADC_RST_GPIO_Port, ADC_RST_Pin, GPIO_PIN_SET);
}

bool adc_protocol_send_command(uint8_t command)
{
  uint8_t rx_dummy = 0U;

  /* CS 在整个命令期间保持低，符合 ADS1220 串口时序要求。 */
  adc_protocol_chip_select(GPIO_PIN_RESET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  if (!adc_protocol_transfer(&command, &rx_dummy, 1U))
  {
    adc_protocol_chip_select(GPIO_PIN_SET);
    return false;
  }
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_protocol_chip_select(GPIO_PIN_SET);
  return true;
}

bool adc_protocol_configure(const ads1220_config_t *config)
{
  uint8_t tx_buf[1U + ADS1220_REG_COUNT] = {0U};
  uint8_t rx_buf[1U + ADS1220_REG_COUNT] = {0U};

  if ((g_spi == NULL) || (config == NULL))
  {
    return false;
  }

  tx_buf[0] = ADS1220_CMD_WREG(ADS1220_REG_CONFIG0, ADS1220_REG_COUNT);
  tx_buf[1] = config->reg[ADS1220_REG_CONFIG0];
  tx_buf[2] = config->reg[ADS1220_REG_CONFIG1];
  tx_buf[3] = config->reg[ADS1220_REG_CONFIG2];
  tx_buf[4] = config->reg[ADS1220_REG_CONFIG3];

  /* 使用一条 WREG 命令连续写入 4 个寄存器。 */
  adc_protocol_chip_select(GPIO_PIN_RESET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  if (!adc_protocol_transfer(tx_buf, rx_buf, (uint16_t)sizeof(tx_buf)))
  {
    adc_protocol_chip_select(GPIO_PIN_SET);
    return false;
  }
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_protocol_chip_select(GPIO_PIN_SET);

  g_expected_config = *config;
  return true;
}

bool adc_protocol_configure_default(void)
{
  ads1220_config_t config = {
    {
      ADS1220_DEFAULT_CONFIG0,
      ADS1220_DEFAULT_CONFIG1,
      ADS1220_DEFAULT_CONFIG2,
      ADS1220_DEFAULT_CONFIG3
    }
  };

  return adc_protocol_configure(&config);
}

bool adc_protocol_read_config(ads1220_config_t *config)
{
  uint8_t tx_buf[1U + ADS1220_REG_COUNT] = {0U};
  uint8_t rx_buf[1U + ADS1220_REG_COUNT] = {0U};

  if ((g_spi == NULL) || (config == NULL))
  {
    return false;
  }

  tx_buf[0] = ADS1220_CMD_RREG(ADS1220_REG_CONFIG0, ADS1220_REG_COUNT);

  /* 读寄存器时，第 1 字节发命令，后续时钟把寄存器内容移出。 */
  adc_protocol_chip_select(GPIO_PIN_RESET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  if (!adc_protocol_transfer(tx_buf, rx_buf, (uint16_t)sizeof(tx_buf)))
  {
    adc_protocol_chip_select(GPIO_PIN_SET);
    return false;
  }
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_protocol_chip_select(GPIO_PIN_SET);

  config->reg[ADS1220_REG_CONFIG0] = rx_buf[1];
  config->reg[ADS1220_REG_CONFIG1] = rx_buf[2];
  config->reg[ADS1220_REG_CONFIG2] = rx_buf[3];
  config->reg[ADS1220_REG_CONFIG3] = rx_buf[4];
  return true;
}

void adc_protocol_reset(void)
{
  /* 同时保留硬件 RST 脚和软件 RESET 命令，便于不同阶段复位器件。 */
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ADC_RST_GPIO_Port, ADC_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(APP_ADC_RESET_PULSE_MS);
  HAL_GPIO_WritePin(ADC_RST_GPIO_Port, ADC_RST_Pin, GPIO_PIN_SET);
  (void)adc_protocol_send_command(ADS1220_CMD_RESET);
  HAL_Delay(APP_ADC_RESET_PULSE_MS);
}

void adc_protocol_stop(void)
{
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_RESET);
  (void)adc_protocol_send_command(ADS1220_CMD_POWERDOWN);
}

void adc_protocol_start_conversion(void)
{
  /* START 引脚脉冲 + START/SYNC 命令，两者都保留，方便后续裁剪策略。 */
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_SET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_RESET);
  (void)adc_protocol_send_command(ADS1220_CMD_START_SYNC);
}

bool adc_protocol_read_raw24(uint8_t data[ADS1220_DATA_BYTES])
{
  if ((g_spi == NULL) || (data == NULL))
  {
    return false;
  }

  /* DRDY 已经就绪后，直接读取 3 字节数据，不再额外发 RDATA。 */
  adc_protocol_chip_select(GPIO_PIN_RESET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  if (HAL_SPI_Receive(g_spi, data, ADS1220_DATA_BYTES, HAL_MAX_DELAY) != HAL_OK)
  {
    adc_protocol_chip_select(GPIO_PIN_SET);
    return false;
  }
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_protocol_chip_select(GPIO_PIN_SET);

  return true;
}

int32_t adc_protocol_parse_raw24(const uint8_t data[ADS1220_DATA_BYTES])
{
  int32_t code;

  /* ADS1220 数据格式为 24 位二进制补码，MSB first。 */
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

bool adc_protocol_read_sample(int32_t *raw_code)
{
  uint8_t data[ADS1220_DATA_BYTES] = {0U};

  /* 给上层一个“直接拿到有符号码值”的简化接口。 */
  if ((raw_code == NULL) || !adc_protocol_read_raw24(data))
  {
    return false;
  }

  *raw_code = adc_protocol_parse_raw24(data);
  return true;
}

bool adc_protocol_link_check(int32_t raw_code)
{
  ads1220_config_t config;

  UNUSED(raw_code);

  /* 当前通信自检策略：
   * 检查寄存器回读是否与期望配置一致，不对码值内容做强假设。 */
  if (!adc_protocol_read_config(&config))
  {
    return false;
  }

  return (memcmp(&config, &g_expected_config, sizeof(config)) == 0);
}
