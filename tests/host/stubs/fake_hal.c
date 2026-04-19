#include "fake_hal.h"

#include <string.h>

#include "adc_protocol.h"

CoreDebug_Type fake_core_debug;
DWT_Type fake_dwt;
uint32_t SystemCoreClock = 170000000UL;

static uint32_t g_tick_ms;
static HAL_StatusTypeDef g_spi_status = HAL_OK;
static HAL_StatusTypeDef g_spi_receive_status = HAL_OK;
static HAL_StatusTypeDef g_dac_status = HAL_OK;
static uint8_t g_config_mismatch;
static uint32_t g_gpio_write_count;
static uint32_t g_tim_start_count;
static uint32_t g_tim_stop_count;
static uint32_t g_start_sync_count;
static uint8_t g_last_spi_tx[16];
static uint16_t g_last_spi_size;
static uint8_t g_last_config[ADS1220_REG_COUNT];
static int32_t g_raw_queue[512];
static uint32_t g_raw_head;
static uint32_t g_raw_tail;
static uint32_t g_raw_count;

/* 函数说明：
 *   重置 fake HAL 全部内部状态。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   清空 tick、SPI/DAC 返回码、GPIO/TIM 计数和 ADC 样本队列，保证测试用例隔离。
 */
void fake_hal_reset(void)
{
  memset(&fake_core_debug, 0, sizeof(fake_core_debug));
  memset(&fake_dwt, 0, sizeof(fake_dwt));
  g_tick_ms = 0U;
  g_spi_status = HAL_OK;
  g_spi_receive_status = HAL_OK;
  g_dac_status = HAL_OK;
  g_config_mismatch = 0U;
  g_gpio_write_count = 0U;
  g_tim_start_count = 0U;
  g_tim_stop_count = 0U;
  g_start_sync_count = 0U;
  memset(g_last_spi_tx, 0, sizeof(g_last_spi_tx));
  g_last_spi_size = 0U;
  g_last_config[0] = ADS1220_DEFAULT_CONFIG0;
  g_last_config[1] = ADS1220_DEFAULT_CONFIG1;
  g_last_config[2] = ADS1220_DEFAULT_CONFIG2;
  g_last_config[3] = ADS1220_DEFAULT_CONFIG3;
  g_raw_head = 0U;
  g_raw_tail = 0U;
  g_raw_count = 0U;
}

/* 函数说明：
 *   设置 fake HAL 当前毫秒 tick。
 * 输入：
 *   tick: 目标毫秒时间。
 * 输出：
 *   无。
 * 作用：
 *   为依赖 HAL_GetTick 的状态机和 USB 批量等待逻辑提供可控时间基准。
 */
void fake_hal_set_tick(uint32_t tick)
{
  g_tick_ms = tick;
}

/* 函数说明：
 *   推进 fake HAL 当前毫秒 tick。
 * 输入：
 *   delta_ms: 增加的毫秒数。
 * 输出：
 *   无。
 * 作用：
 *   用确定性时间推进替代 sleep，驱动超时和等待窗口测试。
 */
void fake_hal_advance_tick(uint32_t delta_ms)
{
  g_tick_ms += delta_ms;
}

/* 函数说明：
 *   设置 SPI 全双工传输返回状态。
 * 输入：
 *   status: 下一次 HAL_SPI_TransmitReceive 返回值。
 * 输出：
 *   无。
 * 作用：
 *   模拟 ADS1220 命令或寄存器访问中的 OK、timeout、error 等情况。
 */
void fake_hal_set_spi_status(HAL_StatusTypeDef status)
{
  g_spi_status = status;
}

/* 函数说明：
 *   设置 SPI 读取返回状态。
 * 输入：
 *   status: 下一次 HAL_SPI_Receive 返回值。
 * 输出：
 *   无。
 * 作用：
 *   模拟 ADS1220 原始 24 bit 数据读取的成功或失败。
 */
void fake_hal_set_spi_receive_status(HAL_StatusTypeDef status)
{
  g_spi_receive_status = status;
}

void fake_hal_set_config_mismatch(uint8_t enabled)
{
  g_config_mismatch = enabled;
}

/* 函数说明：
 *   压入一个 fake ADC 原始码值。
 * 输入：
 *   raw_code: 下一次 HAL_SPI_Receive 输出的 24 bit 原始码值。
 * 输出：
 *   无。
 * 作用：
 *   给 app smoke 测试提供确定性的 ADC 样本序列。
 */
void fake_hal_queue_spi_receive_raw24(int32_t raw_code)
{
  if (g_raw_count >= (uint32_t)(sizeof(g_raw_queue) / sizeof(g_raw_queue[0])))
  {
    return;
  }

  g_raw_queue[g_raw_head] = raw_code;
  g_raw_head = (g_raw_head + 1U) % (uint32_t)(sizeof(g_raw_queue) / sizeof(g_raw_queue[0]));
  g_raw_count++;
}

/* 函数说明：
 *   设置 DAC 相关 fake HAL 返回状态。
 * 输入：
 *   status: HAL_DAC_Start 和 HAL_DAC_SetValue 返回值。
 * 输出：
 *   无。
 * 作用：
 *   允许后续测试模拟前端偏置启动失败。
 */
void fake_hal_set_dac_status(HAL_StatusTypeDef status)
{
  g_dac_status = status;
}

/* 函数说明：
 *   获取 GPIO 写操作次数。
 * 输入：
 *   无。
 * 输出：
 *   返回 fake HAL_GPIO_WritePin 被调用次数。
 * 作用：
 *   为需要验证片选、START、RST 操作的测试保留只读计数。
 */
uint32_t fake_hal_get_gpio_write_count(void)
{
  return g_gpio_write_count;
}

/* 函数说明：
 *   获取 TIM 启动次数。
 * 输入：
 *   无。
 * 输出：
 *   返回 HAL_TIM_Base_Start_IT 被调用次数。
 * 作用：
 *   为 app 状态机测试观察采样定时器启动行为。
 */
uint32_t fake_hal_get_tim_start_count(void)
{
  return g_tim_start_count;
}

/* 函数说明：
 *   获取 TIM 停止次数。
 * 输入：
 *   无。
 * 输出：
 *   返回 HAL_TIM_Base_Stop_IT 被调用次数。
 * 作用：
 *   为 app 状态机测试观察故障或初始化阶段的定时器停止行为。
 */
uint32_t fake_hal_get_tim_stop_count(void)
{
  return g_tim_stop_count;
}

uint32_t fake_hal_get_start_sync_count(void)
{
  return g_start_sync_count;
}

/* 函数说明：
 *   获取最后一次 SPI TX 缓冲区。
 * 输入：
 *   无。
 * 输出：
 *   返回只读 TX 字节缓冲区指针。
 * 作用：
 *   让 adc_protocol 测试验证 ADS1220 命令和寄存器写入字节序。
 */
const uint8_t *fake_hal_get_last_spi_tx(void)
{
  return g_last_spi_tx;
}

/* 函数说明：
 *   获取最后一次 SPI 传输长度。
 * 输入：
 *   无。
 * 输出：
 *   返回最后一次 HAL_SPI_TransmitReceive 的 Size。
 * 作用：
 *   配合 TX 缓冲区验证协议层 SPI 事务边界。
 */
uint16_t fake_hal_get_last_spi_size(void)
{
  return g_last_spi_size;
}

/* 函数说明：
 *   fake HAL 毫秒 tick 查询。
 * 输入：
 *   无。
 * 输出：
 *   返回当前 fake tick。
 * 作用：
 *   替代真实 HAL_GetTick，使 host 测试可确定性推进时间。
 */
uint32_t HAL_GetTick(void)
{
  return g_tick_ms;
}

/* 函数说明：
 *   fake HAL 延时。
 * 输入：
 *   delay_ms: 延时毫秒数。
 * 输出：
 *   无。
 * 作用：
 *   不阻塞主机测试进程，只推进 fake tick。
 */
void HAL_Delay(uint32_t delay_ms)
{
  g_tick_ms += delay_ms;
}

/* 函数说明：
 *   fake GPIO 写引脚。
 * 输入：
 *   GPIOx: GPIO 端口指针。
 *   GPIO_Pin: GPIO 引脚位。
 *   PinState: 输出电平。
 * 输出：
 *   无。
 * 作用：
 *   记录调用次数，不访问真实寄存器。
 */
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
  UNUSED(GPIOx);
  UNUSED(GPIO_Pin);
  UNUSED(PinState);
  g_gpio_write_count++;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  UNUSED(GPIOx);
  UNUSED(GPIO_Pin);
  return GPIO_PIN_SET;
}

/* 函数说明：
 *   fake SPI 全双工传输。
 * 输入：
 *   hspi: SPI 句柄。
 *   pTxData: 发送缓冲区。
 *   pRxData: 接收缓冲区。
 *   Size: 传输长度。
 *   Timeout: 超时时间。
 * 输出：
 *   返回预设 HAL 状态。
 * 作用：
 *   记录 TX 内容，并在 RREG 默认配置场景下回填 ADS1220 默认寄存器值。
 */
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi,
                                           uint8_t *pTxData,
                                           uint8_t *pRxData,
                                           uint16_t Size,
                                           uint32_t Timeout)
{
  UNUSED(hspi);
  UNUSED(Timeout);

  g_last_spi_size = Size;
  memset(g_last_spi_tx, 0, sizeof(g_last_spi_tx));
  if ((pTxData != 0) && (Size <= sizeof(g_last_spi_tx)))
  {
    memcpy(g_last_spi_tx, pTxData, Size);
  }
  if ((pTxData != 0) && (Size == 1U) && (pTxData[0] == ADS1220_CMD_START_SYNC))
  {
    g_start_sync_count++;
  }
  if ((pTxData != 0) && (Size == (uint16_t)(1U + ADS1220_REG_COUNT)) &&
      (pTxData[0] == ADS1220_CMD_WREG(ADS1220_REG_CONFIG0, ADS1220_REG_COUNT)))
  {
    g_last_config[0] = pTxData[1];
    g_last_config[1] = pTxData[2];
    g_last_config[2] = pTxData[3];
    g_last_config[3] = pTxData[4];
  }

  if ((pRxData != 0) && (Size != 0U))
  {
    memset(pRxData, 0, Size);
    if ((pTxData != 0) && (Size == (uint16_t)(1U + ADS1220_REG_COUNT)) &&
        (pTxData[0] == ADS1220_CMD_RREG(ADS1220_REG_CONFIG0, ADS1220_REG_COUNT)))
    {
      pRxData[1] = (g_config_mismatch == 0U) ? g_last_config[0] : (uint8_t)(g_last_config[0] ^ 0x01U);
      pRxData[2] = g_last_config[1];
      pRxData[3] = g_last_config[2];
      pRxData[4] = g_last_config[3];
    }
  }

  return g_spi_status;
}

/* 函数说明：
 *   fake SPI 读取。
 * 输入：
 *   hspi: SPI 句柄。
 *   pData: 接收缓冲区。
 *   Size: 读取长度。
 *   Timeout: 超时时间。
 * 输出：
 *   返回预设 HAL 状态。
 * 作用：
 *   从 fake ADC 样本队列取出一个码值，并按 ADS1220 MSB first 格式写入 3 字节。
 */
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi,
                                   uint8_t *pData,
                                   uint16_t Size,
                                   uint32_t Timeout)
{
  int32_t raw_code = 0;

  UNUSED(hspi);
  UNUSED(Timeout);

  if ((pData != 0) && (Size == ADS1220_DATA_BYTES))
  {
    if (g_raw_count != 0U)
    {
      raw_code = g_raw_queue[g_raw_tail];
      g_raw_tail = (g_raw_tail + 1U) % (uint32_t)(sizeof(g_raw_queue) / sizeof(g_raw_queue[0]));
      g_raw_count--;
    }

    pData[0] = (uint8_t)(((uint32_t)raw_code >> 16) & 0xFFU);
    pData[1] = (uint8_t)(((uint32_t)raw_code >> 8) & 0xFFU);
    pData[2] = (uint8_t)((uint32_t)raw_code & 0xFFU);
  }

  return g_spi_receive_status;
}

/* 函数说明：
 *   fake DAC 启动。
 * 输入：
 *   hdac: DAC 句柄。
 *   channel: DAC 通道。
 * 输出：
 *   返回预设 DAC 状态。
 * 作用：
 *   模拟前端偏置启动，不访问真实 DAC 外设。
 */
HAL_StatusTypeDef HAL_DAC_Start(DAC_HandleTypeDef *hdac, uint32_t channel)
{
  UNUSED(hdac);
  UNUSED(channel);
  return g_dac_status;
}

/* 函数说明：
 *   fake DAC 设置输出值。
 * 输入：
 *   hdac: DAC 句柄。
 *   channel: DAC 通道。
 *   alignment: 数据对齐格式。
 *   value: DAC 输出码值。
 * 输出：
 *   返回预设 DAC 状态。
 * 作用：
 *   模拟默认偏置写入，不访问真实 DAC 外设。
 */
HAL_StatusTypeDef HAL_DAC_SetValue(DAC_HandleTypeDef *hdac, uint32_t channel, uint32_t alignment, uint32_t value)
{
  UNUSED(hdac);
  UNUSED(channel);
  UNUSED(alignment);
  UNUSED(value);
  return g_dac_status;
}

/* 函数说明：
 *   fake TIM 中断启动。
 * 输入：
 *   htim: TIM 句柄。
 * 输出：
 *   返回 HAL_OK。
 * 作用：
 *   记录启动次数，用于观察 app 是否进入采样节拍阶段。
 */
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim)
{
  UNUSED(htim);
  g_tim_start_count++;
  return HAL_OK;
}

/* 函数说明：
 *   fake TIM 中断停止。
 * 输入：
 *   htim: TIM 句柄。
 * 输出：
 *   返回 HAL_OK。
 * 作用：
 *   记录停止次数，用于观察初始化和故障处理路径。
 */
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim)
{
  UNUSED(htim);
  g_tim_stop_count++;
  return HAL_OK;
}

/* 函数说明：
 *   fake 读取 PRIMASK。
 * 输入：
 *   无。
 * 输出：
 *   固定返回 0，表示当前允许中断。
 * 作用：
 *   支持 app 命令标志临界区代码在主机环境编译运行。
 */
uint32_t __get_PRIMASK(void)
{
  return 0U;
}

/* 函数说明：
 *   fake 关闭中断。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   主机测试中不操作真实中断，仅满足 app 临界区依赖。
 */
void __disable_irq(void)
{
}

/* 函数说明：
 *   fake 打开中断。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   主机测试中不操作真实中断，仅满足 app 临界区依赖。
 */
void __enable_irq(void)
{
}

/* 函数说明：
 *   fake 设置 TIM 计数器。
 * 输入：
 *   htim: TIM 句柄。
 *   value: 计数器目标值。
 * 输出：
 *   无。
 * 作用：
 *   替代 __HAL_TIM_SET_COUNTER 宏，避免主机测试访问硬件寄存器。
 */
void fake_hal_tim_set_counter(TIM_HandleTypeDef *htim, uint32_t value)
{
  UNUSED(htim);
  UNUSED(value);
}

/* 函数说明：
 *   fake 清除 TIM 标志。
 * 输入：
 *   htim: TIM 句柄。
 *   flag: 待清除标志。
 * 输出：
 *   无。
 * 作用：
 *   替代 __HAL_TIM_CLEAR_FLAG 宏，避免主机测试访问硬件寄存器。
 */
void fake_hal_tim_clear_flag(TIM_HandleTypeDef *htim, uint32_t flag)
{
  UNUSED(htim);
  UNUSED(flag);
}
