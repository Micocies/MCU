#ifndef VERSION_H
#define VERSION_H

#include <stdint.h>

typedef struct
{
  uint32_t semver_packed;
  uint32_t build_number;
  uint32_t packet_version;
  uint32_t param_signature;
  uint32_t ads1220_default_config;
} version_descriptor_t;

void version_get_descriptor(version_descriptor_t *descriptor);

#endif /* VERSION_H */
