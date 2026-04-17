#ifndef __VERSION_H
#define __VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
  uint32_t semver_packed;
  uint32_t build_number;
  uint32_t packet_version;
  uint32_t param_signature;
  uint32_t ads1220_default_config;
} version_descriptor_t;

#define FW_VERSION_MAJOR                0U
#define FW_VERSION_MINOR                2U
#define FW_VERSION_PATCH                0U
#define FW_BUILD_NUMBER                 2U

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
