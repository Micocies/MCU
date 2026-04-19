#include "ads1220_device.h"

#include <string.h>

#include "adc_bus.h"
#include "project_config.h"

#define ADS1220_CONFIG0_MUX_MASK               0xF0U
#define ADS1220_CONFIG0_MUX_SINGLE_ENDED_AIN0  0x80U

static ads1220_device_t g_ads1220_devices[ADC_DEVICE_COUNT];

static ads1220_config_t ads1220_device_default_config(void)
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

static void ads1220_device_select(const ads1220_device_t *dev)
{
  if (dev != 0)
  {
    adc_bus_select_device(dev->device_id);
  }
}

void ads1220_device_table_init(void)
{
  uint8_t device_id;

  memset(g_ads1220_devices, 0, sizeof(g_ads1220_devices));
  for (device_id = 0U; device_id < ADC_DEVICE_COUNT; ++device_id)
  {
    const ads1220_route_t *route = board_topology_get_route(device_id);

    g_ads1220_devices[device_id].device_id = device_id;
    if (route != 0)
    {
      g_ads1220_devices[device_id].subboard_id = route->subboard_id;
      g_ads1220_devices[device_id].local_device_id = route->local_device_id;
    }
    g_ads1220_devices[device_id].expected_config = ads1220_device_default_config();
    g_ads1220_devices[device_id].last_status = ADC_PROTOCOL_OK;
  }
}

ads1220_device_t *ads1220_device_get(uint8_t device_id)
{
  if (device_id >= ADC_DEVICE_COUNT)
  {
    return 0;
  }

  return &g_ads1220_devices[device_id];
}

adc_protocol_status_t ads1220_device_reset(ads1220_device_t *dev)
{
  if (dev == 0)
  {
    return ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  ads1220_device_select(dev);
  dev->last_status = adc_protocol_reset();
  dev->is_configured = 0U;
  return dev->last_status;
}

adc_protocol_status_t ads1220_device_configure_default(ads1220_device_t *dev)
{
  if (dev == 0)
  {
    return ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  dev->expected_config = ads1220_device_default_config();
  ads1220_device_select(dev);
  dev->last_status = adc_protocol_configure(&dev->expected_config);
  dev->is_configured = (dev->last_status == ADC_PROTOCOL_OK) ? 1U : 0U;
  return dev->last_status;
}

adc_protocol_status_t ads1220_device_configure_channel(ads1220_device_t *dev, uint8_t channel_id)
{
  uint8_t mux_bits;

  if ((dev == 0) || (channel_id >= ADC_CHANNELS_PER_DEVICE))
  {
    return ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  mux_bits = (uint8_t)(ADS1220_CONFIG0_MUX_SINGLE_ENDED_AIN0 + (channel_id << 4));
  dev->expected_config.reg[ADS1220_REG_CONFIG0] =
    (uint8_t)((dev->expected_config.reg[ADS1220_REG_CONFIG0] & 0x0FU) | mux_bits);

  ads1220_device_select(dev);
  dev->last_status = adc_protocol_configure(&dev->expected_config);
  dev->is_configured = (dev->last_status == ADC_PROTOCOL_OK) ? 1U : 0U;
  return dev->last_status;
}

adc_protocol_status_t ads1220_device_start_continuous(ads1220_device_t *dev)
{
  if (dev == 0)
  {
    return ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  ads1220_device_select(dev);
  dev->last_status = adc_protocol_start_continuous(&dev->expected_config);
  return dev->last_status;
}

adc_protocol_status_t ads1220_device_read_sample(ads1220_device_t *dev, int32_t *raw_code)
{
  if ((dev == 0) || (raw_code == 0))
  {
    return ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  ads1220_device_select(dev);
  dev->last_status = adc_protocol_read_sample(raw_code);
  if (dev->last_status == ADC_PROTOCOL_OK)
  {
    dev->last_raw_code = *raw_code;
  }

  return dev->last_status;
}

adc_protocol_status_t ads1220_device_link_check(ads1220_device_t *dev)
{
  if (dev == 0)
  {
    return ADC_PROTOCOL_ERR_INVALID_ARG;
  }

  ads1220_device_select(dev);
  dev->last_status = adc_protocol_link_check(&dev->expected_config);
  return dev->last_status;
}
