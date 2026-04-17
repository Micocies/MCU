#include "fake_hal.h"
#include "version.h"

DAC_HandleTypeDef hdac1;
SPI_HandleTypeDef hspi1;
TIM_HandleTypeDef htim6;

GPIO_TypeDef fake_gpioa;
GPIO_TypeDef fake_gpiob;

/* 函数说明：
 *   提供测试用版本描述符。
 * 输入：
 *   descriptor: 版本描述符输出指针。
 * 输出：
 *   无。
 * 作用：
 *   让 app.c 的元信息发送路径在 host 测试中可链接，避免拉入正式版本头的额外依赖。
 */
void version_get_descriptor(version_descriptor_t *descriptor)
{
  if (descriptor == 0)
  {
    return;
  }

  descriptor->semver_packed = 0x00000100UL;
  descriptor->build_number = 1U;
  descriptor->packet_version = 2U;
  descriptor->param_signature = 0x12345678UL;
  descriptor->ads1220_default_config = 0x00100408UL;
}
