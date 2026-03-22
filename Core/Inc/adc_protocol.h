#ifndef __ADC_PROTOCOL_H
#define __ADC_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "main.h"

/* ADS1220 只有 4 个配置寄存器，按地址顺序统一保存。 */
typedef enum
{
  ADS1220_REG_CONFIG0 = 0U,
  ADS1220_REG_CONFIG1 = 1U,
  ADS1220_REG_CONFIG2 = 2U,
  ADS1220_REG_CONFIG3 = 3U,
  ADS1220_REG_COUNT = 4U
} ads1220_register_t;

typedef struct
{
  uint8_t reg[ADS1220_REG_COUNT];
} ads1220_config_t;

/* ADS1220 普通转换结果固定输出 24 bit，MSB first。 */
#define ADS1220_DATA_BYTES               3U

/* 数据手册定义的基础命令。 */
#define ADS1220_CMD_POWERDOWN            0x02U
#define ADS1220_CMD_RESET                0x06U
#define ADS1220_CMD_START_SYNC           0x08U
#define ADS1220_CMD_RDATA                0x10U
#define ADS1220_CMD_RREG(addr, count)    (uint8_t)(0x20U | (((uint8_t)(addr) & 0x03U) << 2) | (((uint8_t)(count) - 1U) & 0x03U))
#define ADS1220_CMD_WREG(addr, count)    (uint8_t)(0x40U | (((uint8_t)(addr) & 0x03U) << 2) | (((uint8_t)(count) - 1U) & 0x03U))

/* 当前工程使用的一套默认启动值，后续可按通道、增益、速率继续细化。 */
#define ADS1220_DEFAULT_CONFIG0          0x08U
#define ADS1220_DEFAULT_CONFIG1          0x04U
#define ADS1220_DEFAULT_CONFIG2          0x10U
#define ADS1220_DEFAULT_CONFIG3          0x00U

void adc_protocol_init(SPI_HandleTypeDef *hspi);
/* 一次性写入 4 个配置寄存器。 */
bool adc_protocol_configure(const ads1220_config_t *config);
/* 使用工程默认值配置 ADS1220。 */
bool adc_protocol_configure_default(void);
/* 回读 4 个配置寄存器，常用于上电自检。 */
bool adc_protocol_read_config(ads1220_config_t *config);
/* 发送单字节控制命令。 */
bool adc_protocol_send_command(uint8_t command);
void adc_protocol_reset(void);
void adc_protocol_stop(void);
void adc_protocol_start_conversion(void);
/* DRDY 就绪后直接读取 3 字节原始码，不额外发送 RDATA。 */
bool adc_protocol_read_raw24(uint8_t data[ADS1220_DATA_BYTES]);
/* 24 位二进制补码扩展为 32 位有符号整数。 */
int32_t adc_protocol_parse_raw24(const uint8_t data[ADS1220_DATA_BYTES]);
/* 按 Vref 和 Gain 把 ADC 码值换算为输入差分电压。 */
float adc_protocol_code_to_voltage(int32_t code, float vref, float gain);
bool adc_protocol_read_sample(int32_t *raw_code);
bool adc_protocol_link_check(int32_t raw_code);

#ifdef __cplusplus
}
#endif

#endif /* __ADC_PROTOCOL_H */

