#include "version.h"

#include "adc_protocol.h"
#include "app_config.h"

/* Keep firmware identity and frozen-parameter hashing in one place. */
#define VERSION_FNV1A_INIT             2166136261UL
#define VERSION_FNV1A_PRIME            16777619UL

static uint32_t version_hash_accumulate_byte(uint32_t hash, uint8_t value)
{
  hash ^= value;
  hash *= VERSION_FNV1A_PRIME;
  return hash;
}

static uint32_t version_hash_accumulate_u32_le(uint32_t hash, uint32_t value)
{
  hash = version_hash_accumulate_byte(hash, (uint8_t)(value & 0xFFU));
  hash = version_hash_accumulate_byte(hash, (uint8_t)((value >> 8) & 0xFFU));
  hash = version_hash_accumulate_byte(hash, (uint8_t)((value >> 16) & 0xFFU));
  hash = version_hash_accumulate_byte(hash, (uint8_t)((value >> 24) & 0xFFU));
  return hash;
}

uint32_t version_get_semver_packed(void)
{
  return ((FW_VERSION_MAJOR & 0xFFUL) << 16)
       | ((FW_VERSION_MINOR & 0xFFUL) << 8)
       | (FW_VERSION_PATCH & 0xFFUL);
}

uint32_t version_get_build_number(void)
{
  return FW_BUILD_NUMBER;
}

uint32_t version_get_packet_version(void)
{
  return SAMPLE_PACKET_VERSION;
}

uint32_t version_get_ads1220_default_config(void)
{
  return ((uint32_t)ADS1220_DEFAULT_CONFIG3 << 24)
       | ((uint32_t)ADS1220_DEFAULT_CONFIG2 << 16)
       | ((uint32_t)ADS1220_DEFAULT_CONFIG1 << 8)
       | (uint32_t)ADS1220_DEFAULT_CONFIG0;
}

uint32_t version_get_param_signature(void)
{
  uint32_t hash = VERSION_FNV1A_INIT;

  /* Serialize each u32 in little-endian order so the host can reproduce the
   * exact same standard FNV-1a result across architectures. */
  hash = version_hash_accumulate_u32_le(hash, version_get_packet_version());
  hash = version_hash_accumulate_u32_le(hash, APP_SAMPLE_RATE_HZ);
  hash = version_hash_accumulate_u32_le(hash, APP_BIAS_STABILIZE_MS);
  hash = version_hash_accumulate_u32_le(hash, APP_DARK_CALIBRATION_SAMPLES);
  hash = version_hash_accumulate_u32_le(hash, APP_DRDY_TIMEOUT_MS);
  hash = version_hash_accumulate_u32_le(hash, APP_FILTER_ALPHA_SHIFT);
  hash = version_hash_accumulate_u32_le(hash, APP_USB_QUEUE_DEPTH);
  hash = version_hash_accumulate_u32_le(hash, APP_DAC_BIAS_CH1);
  hash = version_hash_accumulate_u32_le(hash, APP_DAC_BIAS_CH2);
  hash = version_hash_accumulate_u32_le(hash, APP_RECOVERY_SPI_RETRY_LIMIT);
  hash = version_hash_accumulate_u32_le(hash, APP_RECOVERY_RECONFIGURE_LIMIT);
  hash = version_hash_accumulate_u32_le(hash, APP_RECOVERY_HOLD_THRESHOLD);
  hash = version_hash_accumulate_u32_le(hash, APP_ADC_LINK_CHECK_RETRIES);
  hash = version_hash_accumulate_u32_le(hash, version_get_ads1220_default_config());
  return hash;
}

void version_get_descriptor(version_descriptor_t *descriptor)
{
  if (descriptor == 0)
  {
    return;
  }

  descriptor->semver_packed = version_get_semver_packed();
  descriptor->build_number = version_get_build_number();
  descriptor->packet_version = version_get_packet_version();
  descriptor->param_signature = version_get_param_signature();
  descriptor->ads1220_default_config = version_get_ads1220_default_config();
}
