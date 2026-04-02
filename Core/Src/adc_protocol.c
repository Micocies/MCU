#include "adc_protocol.h"

#include <string.h>

#include "app_config.h"

static SPI_HandleTypeDef *g_spi = NULL;
/* 保存期望寄存器镜像。
 * COMM_CHECK 阶段通过回读寄存器确认器件配置没有写偏。 */
static ads1220_config_t g_expected_config = {
  {
    ADS1220_DEFAULT_CONFIG0,
    ADS1220_DEFAULT_CONFIG1,
    ADS1220_DEFAULT_CONFIG2,
    ADS1220_DEFAULT_CONFIG3
  }
};

/* 函数说明：
 *   控制 ADS1220 片选信号。
 * 输入：
 *   state: 片选输出电平。
 * 输出：
 *   无。
 * 作用：
 *   在 SPI 事务前后切换通信边界。
 */
static void adc_protocol_chip_select(GPIO_PinState state)
{
  HAL_GPIO_WritePin(ADC_CS_GPIO_Port, ADC_CS_Pin, state);
}

/* 函数说明：
 *   插入少量空转周期。
 * 输入：
 *   cycles: 空转周期数。
 * 输出：
 *   无。
 * 作用：
 *   在命令或读写动作前后给 SPI 事务留出边界。
 */
static void adc_protocol_delay_cycles(uint32_t cycles)
{
  uint32_t i;

  for (i = 0U; i < cycles; ++i)
  {
    __NOP();
  }
}

/* 函数说明：
 *   将 HAL SPI 返回值映射为协议层状态码。
 * 输入：
 *   hal_status: HAL SPI 返回值。
 * 输出：
 *   协议层状态码。
 * 作用：
 *   区分超时和一般 SPI 错误，便于上层诊断。
 */
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

/* 函数说明：
 *   封装 ADS1220 的基础 SPI 全双工传输。
 * 输入：
 *   tx_buf: 发送缓冲区。
 *   rx_buf: 接收缓冲区。
 *   size: 传输字节数。
 *   timeout_ms: SPI 事务超时时间，单位 ms。
 * 输出：
 *   返回协议层状态码。
 * 作用：
 *   统一底层 SPI 读写接口。
 */
static adc_protocol_status_t adc_protocol_transfer(const uint8_t *tx_buf,
                                                   uint8_t *rx_buf,
                                                   uint16_t size,
                                                   uint32_t timeout_ms)
{
  HAL_StatusTypeDef hal_status;

  if ((g_spi == NULL) || (size == 0U))
  {
    return (g_spi == NULL) ? ADC_PROTOCOL_ERR_NOT_INIT : ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  hal_status = HAL_SPI_TransmitReceive(g_spi, (uint8_t *)tx_buf, rx_buf, size, timeout_ms);
  return adc_protocol_status_from_hal(hal_status);
}

/* 函数说明：
 *   初始化协议层。
 * 输入：
 *   hspi: SPI 句柄。
 * 输出：
 *   无。
 * 作用：
 *   绑定 SPI 外设，并设置 ADS1220 的空闲引脚状态。
 */
void adc_protocol_init(SPI_HandleTypeDef *hspi)
{
  g_spi = hspi;
  adc_protocol_chip_select(GPIO_PIN_SET);
  /* START 和 RST 在空闲时保持默认电平，避免上电时误触发。 */
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ADC_RST_GPIO_Port, ADC_RST_Pin, GPIO_PIN_SET);
}

/* 函数说明：
 *   发送 ADS1220 单字节命令。
 * 输入：
 *   command: ADS1220 单字节命令。
 * 输出：
 *   返回协议层状态码。
 * 作用：
 *   发送 RESET、START/SYNC、POWERDOWN 等控制命令。
 */
adc_protocol_status_t adc_protocol_send_command(uint8_t command)
{
  uint8_t rx_dummy = 0U;
  adc_protocol_status_t status;

  /* CS 在整个命令期间保持低，符合 ADS1220 串口时序要求。 */
  adc_protocol_chip_select(GPIO_PIN_RESET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  status = adc_protocol_transfer(&command, &rx_dummy, 1U, APP_ADC_SPI_TIMEOUT_INIT_MS);
  if (status != ADC_PROTOCOL_OK)
  {
    adc_protocol_chip_select(GPIO_PIN_SET);
    return status;
  }
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_protocol_chip_select(GPIO_PIN_SET);
  return ADC_PROTOCOL_OK;
}

/* 函数说明：
 *   写入 ADS1220 配置寄存器。
 * 输入：
 *   config: 目标寄存器配置。
 * 输出：
 *   返回协议层状态码。
 * 作用：
 *   通过一条 WREG 命令连续写入 4 个配置寄存器。
 */
adc_protocol_status_t adc_protocol_configure(const ads1220_config_t *config)
{
  uint8_t tx_buf[1U + ADS1220_REG_COUNT] = {0U};
  uint8_t rx_buf[1U + ADS1220_REG_COUNT] = {0U};
  adc_protocol_status_t status;

  if ((g_spi == NULL) || (config == NULL))
  {
    return (g_spi == NULL) ? ADC_PROTOCOL_ERR_NOT_INIT : ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  tx_buf[0] = ADS1220_CMD_WREG(ADS1220_REG_CONFIG0, ADS1220_REG_COUNT);
  tx_buf[1] = config->reg[ADS1220_REG_CONFIG0];
  tx_buf[2] = config->reg[ADS1220_REG_CONFIG1];
  tx_buf[3] = config->reg[ADS1220_REG_CONFIG2];
  tx_buf[4] = config->reg[ADS1220_REG_CONFIG3];

  /* 使用一条 WREG 命令连续写入 4 个寄存器。 */
  adc_protocol_chip_select(GPIO_PIN_RESET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  status = adc_protocol_transfer(tx_buf, rx_buf, (uint16_t)sizeof(tx_buf), APP_ADC_SPI_TIMEOUT_INIT_MS);
  if (status != ADC_PROTOCOL_OK)
  {
    adc_protocol_chip_select(GPIO_PIN_SET);
    return status;
  }
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_protocol_chip_select(GPIO_PIN_SET);

  g_expected_config = *config;
  return ADC_PROTOCOL_OK;
}

/* 函数说明：
 *   写入工程默认配置。
 * 输入：
 *   无。
 * 输出：
 *   返回协议层状态码。
 * 作用：
 *   使用工程内定义的默认寄存器值配置 ADS1220。
 */
adc_protocol_status_t adc_protocol_configure_default(void)
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

/* 函数说明：
 *   回读 ADS1220 配置寄存器。
 * 输入：
 *   config: 配置输出指针。
 * 输出：
 *   返回协议层状态码。
 * 作用：
 *   读取 ADS1220 的 4 个配置寄存器，用于通信自检或调试。
 */
adc_protocol_status_t adc_protocol_read_config(ads1220_config_t *config)
{
  uint8_t tx_buf[1U + ADS1220_REG_COUNT] = {0U};
  uint8_t rx_buf[1U + ADS1220_REG_COUNT] = {0U};
  adc_protocol_status_t status;

  if ((g_spi == NULL) || (config == NULL))
  {
    return (g_spi == NULL) ? ADC_PROTOCOL_ERR_NOT_INIT : ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  tx_buf[0] = ADS1220_CMD_RREG(ADS1220_REG_CONFIG0, ADS1220_REG_COUNT);

  /* 读寄存器时，第 1 字节发命令，后续时钟把寄存器内容移出。 */
  adc_protocol_chip_select(GPIO_PIN_RESET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  status = adc_protocol_transfer(tx_buf, rx_buf, (uint16_t)sizeof(tx_buf), APP_ADC_SPI_TIMEOUT_INIT_MS);
  if (status != ADC_PROTOCOL_OK)
  {
    adc_protocol_chip_select(GPIO_PIN_SET);
    return status;
  }
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_protocol_chip_select(GPIO_PIN_SET);

  config->reg[ADS1220_REG_CONFIG0] = rx_buf[1];
  config->reg[ADS1220_REG_CONFIG1] = rx_buf[2];
  config->reg[ADS1220_REG_CONFIG2] = rx_buf[3];
  config->reg[ADS1220_REG_CONFIG3] = rx_buf[4];
  return ADC_PROTOCOL_OK;
}

/* 函数说明：
 *   复位 ADS1220。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   同时通过硬件复位脚和 RESET 命令复位 ADS1220。
 */
adc_protocol_status_t adc_protocol_reset(void)
{
  adc_protocol_status_t status;

  /* 同时保留硬件 RST 脚和软件 RESET 命令，便于不同阶段复位器件。 */
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ADC_RST_GPIO_Port, ADC_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(APP_ADC_RESET_PULSE_MS);
  HAL_GPIO_WritePin(ADC_RST_GPIO_Port, ADC_RST_Pin, GPIO_PIN_SET);
  status = adc_protocol_send_command(ADS1220_CMD_RESET);
  HAL_Delay(APP_ADC_RESET_PULSE_MS);
  return status;
}

/* 函数说明：
 *   停止 ADS1220 转换。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   停止转换并发送 POWERDOWN 命令，让 ADS1220 进入低功耗态。
 */
adc_protocol_status_t adc_protocol_stop(void)
{
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_RESET);
  return adc_protocol_send_command(ADS1220_CMD_POWERDOWN);
}

/* 函数说明：
 *   发起一次 ADS1220 转换。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   通过 START 引脚脉冲和 START/SYNC 命令发起一次转换。
 */
adc_protocol_status_t adc_protocol_start_conversion(void)
{
  /* START 引脚脉冲和 START/SYNC 命令都保留，方便后续裁剪策略。 */
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_SET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  HAL_GPIO_WritePin(ADC_START_GPIO_Port, ADC_START_Pin, GPIO_PIN_RESET);
  return adc_protocol_send_command(ADS1220_CMD_START_SYNC);
}

/* 函数说明：
 *   读取 ADS1220 原始 24 位结果。
 * 输入：
 *   data: 3 字节原始数据输出缓冲区。
 * 输出：
 *   返回协议层状态码。
 * 作用：
 *   在 DRDY 就绪后直接读取 ADS1220 的 24 位原始结果。
 */
adc_protocol_status_t adc_protocol_read_raw24(uint8_t data[ADS1220_DATA_BYTES])
{
  HAL_StatusTypeDef hal_status;

  if ((g_spi == NULL) || (data == NULL))
  {
    return (g_spi == NULL) ? ADC_PROTOCOL_ERR_NOT_INIT : ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  /* DRDY 已经就绪后，直接读取 3 字节数据，不再额外发送 RDATA。 */
  adc_protocol_chip_select(GPIO_PIN_RESET);
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  hal_status = HAL_SPI_Receive(g_spi, data, ADS1220_DATA_BYTES, APP_ADC_SPI_TIMEOUT_RUN_MS);
  if (hal_status != HAL_OK)
  {
    adc_protocol_chip_select(GPIO_PIN_SET);
    return adc_protocol_status_from_hal(hal_status);
  }
  adc_protocol_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  adc_protocol_chip_select(GPIO_PIN_SET);

  return ADC_PROTOCOL_OK;
}

/* 函数说明：
 *   解析 24 位原始 ADC 数据。
 * 输入：
 *   data: 3 字节原始 ADC 数据。
 * 输出：
 *   返回 32 位有符号码值。
 * 作用：
 *   对 ADS1220 的 24 位补码结果做符号扩展，转换为 MCU 易处理的整数。
 */
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

/* 函数说明：
 *   将 ADC 码值换算为输入差分电压。
 * 输入：
 *   code: ADC 码值。
 *   vref: 参考电压。
 *   gain: PGA 增益。
 * 输出：
 *   返回输入差分电压。
 * 作用：
 *   按 ADS1220 的满量程关系把码值换算为电压。
 */
float adc_protocol_code_to_voltage(int32_t code, float vref, float gain)
{
  if (gain == 0.0f)
  {
    return 0.0f;
  }

  return ((float)code) * ((2.0f * vref) / gain) / 16777216.0f;
}

/* 函数说明：
 *   读取一帧并直接返回有符号原始码值。
 * 输入：
 *   raw_code: 原始码值输出指针。
 * 输出：
 *   返回协议层状态码。
 * 作用：
 *   对上层提供“一次读回有符号原始码”的简化接口。
 */
adc_protocol_status_t adc_protocol_read_sample(int32_t *raw_code)
{
  uint8_t data[ADS1220_DATA_BYTES] = {0U};
  adc_protocol_status_t status;

  /* 给上层一个“直接拿到有符号码值”的简化接口。 */
  if (raw_code == NULL)
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

/* 函数说明：
 *   执行 ADS1220 通信链路自检。
 * 输入：
 *   raw_code: 当前样本码值，当前实现未直接使用。
 * 输出：
 *   返回协议层状态码。
 * 作用：
 *   回读寄存器并与期望配置比较，作为 ADS1220 通信自检依据。
 */
adc_protocol_status_t adc_protocol_link_check(int32_t raw_code)
{
  ads1220_config_t config;
  adc_protocol_status_t status;

  UNUSED(raw_code);

  /* 当前通信自检策略：
   * 检查寄存器回读是否与期望配置一致，不对码值内容做强假设。 */
  status = adc_protocol_read_config(&config);
  if (status != ADC_PROTOCOL_OK)
  {
    return status;
  }

  if (memcmp(&config, &g_expected_config, sizeof(config)) != 0)
  {
    return ADC_PROTOCOL_ERR_CONFIG_MISMATCH;
  }

  return ADC_PROTOCOL_OK;
}

