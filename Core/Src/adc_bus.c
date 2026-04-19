#include "adc_bus.h"

#include "app_config.h"
#include "board_topology.h"

static adc_bus_t *g_adc_bus;

static void adc_bus_delay_cycles(uint32_t cycles)
{
  uint32_t i;

  for (i = 0U; i < cycles; ++i)
  {
    __NOP();
  }
}

static void adc_bus_write_bit(GPIO_TypeDef *port, uint16_t pin, uint8_t value)
{
  HAL_GPIO_WritePin(port, pin, (value != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void adc_bus_init(adc_bus_t *bus, SPI_HandleTypeDef *hspi)
{
  if (bus == 0)
  {
    return;
  }

  bus->hspi = hspi;
  bus->selected_device_id = 0xFFU;
  g_adc_bus = bus;

  adc_bus_cs_deassert();
  HAL_GPIO_WritePin(ADC_START_ALL_GPIO_Port, ADC_START_ALL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ADC_RST_ALL_GPIO_Port, ADC_RST_ALL_Pin, GPIO_PIN_SET);
}

void adc_bus_select_subboard(uint8_t subboard_id)
{
  adc_bus_write_bit(SUB_SEL_A0_GPIO_Port, SUB_SEL_A0_Pin, (uint8_t)(subboard_id & 0x01U));
  adc_bus_write_bit(SUB_SEL_A1_GPIO_Port, SUB_SEL_A1_Pin, (uint8_t)((subboard_id >> 1) & 0x01U));
  adc_bus_write_bit(SUB_SEL_A2_GPIO_Port, SUB_SEL_A2_Pin, (uint8_t)((subboard_id >> 2) & 0x01U));
}

void adc_bus_select_local_device(uint8_t local_device_id)
{
  adc_bus_write_bit(ADC_SEL_A0_GPIO_Port, ADC_SEL_A0_Pin, (uint8_t)(local_device_id & 0x01U));
  adc_bus_write_bit(ADC_SEL_A1_GPIO_Port, ADC_SEL_A1_Pin, (uint8_t)((local_device_id >> 1) & 0x01U));
  adc_bus_write_bit(ADC_SEL_A2_GPIO_Port, ADC_SEL_A2_Pin, (uint8_t)((local_device_id >> 2) & 0x01U));
}

void adc_bus_select_device(uint8_t device_id)
{
  const ads1220_route_t *route;

  route = board_topology_get_route(device_id);
  if (route == 0)
  {
    return;
  }

  adc_bus_select_subboard(route->subboard_id);
  adc_bus_select_local_device(route->local_device_id);
  adc_bus_delay_cycles(16U);

  if (g_adc_bus != 0)
  {
    g_adc_bus->selected_device_id = device_id;
  }
}

void adc_bus_cs_assert(void)
{
  HAL_GPIO_WritePin(ADC_CS_GATE_GPIO_Port, ADC_CS_GATE_Pin, GPIO_PIN_RESET);
}

void adc_bus_cs_deassert(void)
{
  HAL_GPIO_WritePin(ADC_CS_GATE_GPIO_Port, ADC_CS_GATE_Pin, GPIO_PIN_SET);
}

bool adc_bus_is_selected_drdy_low(void)
{
  return (HAL_GPIO_ReadPin(ADC_DRDY_MUX_GPIO_Port, ADC_DRDY_MUX_Pin) == GPIO_PIN_RESET);
}

void adc_bus_reset_all(void)
{
  adc_bus_cs_deassert();
  HAL_GPIO_WritePin(ADC_START_ALL_GPIO_Port, ADC_START_ALL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ADC_RST_ALL_GPIO_Port, ADC_RST_ALL_Pin, GPIO_PIN_RESET);
  HAL_Delay(APP_ADC_RESET_PULSE_MS);
  HAL_GPIO_WritePin(ADC_RST_ALL_GPIO_Port, ADC_RST_ALL_Pin, GPIO_PIN_SET);
  HAL_Delay(APP_ADC_RESET_PULSE_MS);
}

void adc_bus_start_all_pulse(void)
{
  HAL_GPIO_WritePin(ADC_START_ALL_GPIO_Port, ADC_START_ALL_Pin, GPIO_PIN_SET);
  adc_bus_delay_cycles(APP_ADC_START_PULSE_CYCLES);
  HAL_GPIO_WritePin(ADC_START_ALL_GPIO_Port, ADC_START_ALL_Pin, GPIO_PIN_RESET);
}

HAL_StatusTypeDef adc_bus_txrx(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t size, uint32_t timeout_ms)
{
  if ((g_adc_bus == 0) || (g_adc_bus->hspi == 0))
  {
    return HAL_ERROR;
  }

  return HAL_SPI_TransmitReceive(g_adc_bus->hspi, (uint8_t *)tx_buf, rx_buf, size, timeout_ms);
}

HAL_StatusTypeDef adc_bus_rx(uint8_t *rx_buf, uint16_t size, uint32_t timeout_ms)
{
  if ((g_adc_bus == 0) || (g_adc_bus->hspi == 0))
  {
    return HAL_ERROR;
  }

  return HAL_SPI_Receive(g_adc_bus->hspi, rx_buf, size, timeout_ms);
}
