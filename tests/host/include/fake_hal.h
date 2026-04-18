#ifndef FAKE_HAL_H
#define FAKE_HAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  HAL_OK = 0x00U,
  HAL_ERROR = 0x01U,
  HAL_BUSY = 0x02U,
  HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

typedef enum
{
  GPIO_PIN_RESET = 0U,
  GPIO_PIN_SET
} GPIO_PinState;

typedef struct { uint32_t dummy; } SPI_HandleTypeDef;
typedef struct { uint32_t dummy; } DAC_HandleTypeDef;
typedef struct { uint32_t dummy; } TIM_HandleTypeDef;
typedef struct { uint32_t dummy; } GPIO_TypeDef;

typedef struct
{
  volatile uint32_t DEMCR;
} CoreDebug_Type;

typedef struct
{
  volatile uint32_t CTRL;
  volatile uint32_t CYCCNT;
} DWT_Type;

extern CoreDebug_Type fake_core_debug;
extern DWT_Type fake_dwt;
extern GPIO_TypeDef fake_gpioa;
extern GPIO_TypeDef fake_gpiob;

#define CoreDebug (&fake_core_debug)
#define DWT (&fake_dwt)
#define GPIOA (&fake_gpioa)
#define GPIOB (&fake_gpiob)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24)
#define DWT_CTRL_CYCCNTENA_Msk (1UL << 0)

#define GPIO_PIN_0 0x0001U
#define GPIO_PIN_1 0x0002U
#define GPIO_PIN_2 0x0004U
#define GPIO_PIN_15 0x8000U

#define DAC_CHANNEL_1 1U
#define DAC_CHANNEL_2 2U
#define DAC_ALIGN_12B_R 0U
#define TIM_FLAG_UPDATE 0x0001U

#define UNUSED(x) ((void)(x))
#define __NOP() do { } while (0)
#define __HAL_TIM_SET_COUNTER(htim, value) fake_hal_tim_set_counter((htim), (value))
#define __HAL_TIM_CLEAR_FLAG(htim, flag) fake_hal_tim_clear_flag((htim), (flag))

extern uint32_t SystemCoreClock;

void fake_hal_reset(void);
void fake_hal_set_tick(uint32_t tick);
void fake_hal_advance_tick(uint32_t delta_ms);
void fake_hal_set_spi_status(HAL_StatusTypeDef status);
void fake_hal_set_spi_receive_status(HAL_StatusTypeDef status);
void fake_hal_set_config_mismatch(uint8_t enabled);
void fake_hal_queue_spi_receive_raw24(int32_t raw_code);
void fake_hal_set_dac_status(HAL_StatusTypeDef status);
uint32_t fake_hal_get_gpio_write_count(void);
uint32_t fake_hal_get_tim_start_count(void);
uint32_t fake_hal_get_tim_stop_count(void);
uint32_t fake_hal_get_start_sync_count(void);
const uint8_t *fake_hal_get_last_spi_tx(void);
uint16_t fake_hal_get_last_spi_size(void);

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay_ms);
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi,
                                           uint8_t *pTxData,
                                           uint8_t *pRxData,
                                           uint16_t Size,
                                           uint32_t Timeout);
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi,
                                   uint8_t *pData,
                                   uint16_t Size,
                                   uint32_t Timeout);
HAL_StatusTypeDef HAL_DAC_Start(DAC_HandleTypeDef *hdac, uint32_t channel);
HAL_StatusTypeDef HAL_DAC_SetValue(DAC_HandleTypeDef *hdac, uint32_t channel, uint32_t alignment, uint32_t value);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);
void fake_hal_tim_set_counter(TIM_HandleTypeDef *htim, uint32_t value);
void fake_hal_tim_clear_flag(TIM_HandleTypeDef *htim, uint32_t flag);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_HAL_H */
