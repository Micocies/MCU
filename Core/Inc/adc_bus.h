#ifndef __ADC_BUS_H
#define __ADC_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "main.h"

typedef struct
{
  SPI_HandleTypeDef *hspi;
  uint8_t selected_device_id;
} adc_bus_t;

void adc_bus_init(adc_bus_t *bus, SPI_HandleTypeDef *hspi);
void adc_bus_select_subboard(uint8_t subboard_id);
void adc_bus_select_local_device(uint8_t local_device_id);
void adc_bus_select_device(uint8_t device_id);
void adc_bus_cs_assert(void);
void adc_bus_cs_deassert(void);
bool adc_bus_is_selected_drdy_low(void);
void adc_bus_reset_all(void);
void adc_bus_start_all_pulse(void);
HAL_StatusTypeDef adc_bus_txrx(const uint8_t *tx_buf, uint8_t *rx_buf, uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef adc_bus_rx(uint8_t *rx_buf, uint16_t size, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __ADC_BUS_H */
