/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SUB_SEL_A0_Pin GPIO_PIN_0
#define SUB_SEL_A0_GPIO_Port GPIOA
#define SUB_SEL_A1_Pin GPIO_PIN_1
#define SUB_SEL_A1_GPIO_Port GPIOA
#define SUB_SEL_A2_Pin GPIO_PIN_2
#define SUB_SEL_A2_GPIO_Port GPIOA
#define ADC_SEL_A0_Pin GPIO_PIN_3
#define ADC_SEL_A0_GPIO_Port GPIOA
#define ADC_DRDY_MUX_Pin GPIO_PIN_0
#define ADC_DRDY_MUX_GPIO_Port GPIOB
#define ADC_RST_ALL_Pin GPIO_PIN_1
#define ADC_RST_ALL_GPIO_Port GPIOB
#define ADC_START_ALL_Pin GPIO_PIN_2
#define ADC_START_ALL_GPIO_Port GPIOB
#define ADC_SEL_A1_Pin GPIO_PIN_8
#define ADC_SEL_A1_GPIO_Port GPIOA
#define ADC_SEL_A2_Pin GPIO_PIN_9
#define ADC_SEL_A2_GPIO_Port GPIOA
#define ADC_CS_GATE_Pin GPIO_PIN_15
#define ADC_CS_GATE_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
