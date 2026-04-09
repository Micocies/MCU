#ifndef __VERSION_H
#define __VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
  uint32_t semver_packed;   // 3 bytes 语义化版本号，按大端序打包为一个 u32，格式为 0x00MMmmpp，其中 MM、mm、pp 分别是主、次、修版本号
  uint32_t build_number;    // 构建序号，编译时自动递增，便于区分同一语义版本的不同构建
  uint32_t packet_version;  // USB 数据包格式版本，便于上位机解析兼容
  uint32_t param_signature; // 基于 FNV-1a 的参数签名，涵盖所有影响数据包内容的编译期参数，便于上位机检测运行时参数与编译时参数是否一致
  uint32_t ads1220_default_config;  // 编译时 ADS1220 默认寄存器配置的 CRC32 或 FNV-1a 哈希，便于上位机检测运行时配置是否与编译时默认值一致
} version_descriptor_t;

#define FW_VERSION_MAJOR                0U
#define FW_VERSION_MINOR                1U
#define FW_VERSION_PATCH                0U
#define FW_BUILD_NUMBER                 1U

uint32_t version_get_semver_packed(void);
uint32_t version_get_build_number(void);
uint32_t version_get_packet_version(void);
/* Standard FNV-1a over a fixed little-endian u32 field sequence. */
uint32_t version_get_param_signature(void);
uint32_t version_get_ads1220_default_config(void);
void version_get_descriptor(version_descriptor_t *descriptor);

#ifdef __cplusplus
}
#endif

#endif /* __VERSION_H */
