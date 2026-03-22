/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file         stm32g4xx_hal_msp.c
  * @brief        MSP Initialization and De-Initialization code.
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

#include "main.h"

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/* 函数说明：
 *   初始化全局 MSP。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   开启系统基础时钟并完成芯片级通用设置。
 */
void HAL_MspInit(void)
{
  /* USER CODE BEGIN MspInit 0 */
  /* USER CODE END MspInit 0 */

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();

  /* 关闭 UCPD 死电池内部上拉，避免与当前硬件设计无关的额外负载。 */
  HAL_PWREx_DisableUCPDDeadBattery();

  /* USER CODE BEGIN MspInit 1 */
  /* USER CODE END MspInit 1 */
}

/* 函数说明：
 *   初始化 DAC1 的 MSP 资源。
 * 输入：
 *   hdac: DAC 句柄指针。
 * 输出：
 *   无。
 * 作用：
 *   为 DAC1 开启时钟并配置对应模拟输出引脚。
 */
void HAL_DAC_MspInit(DAC_HandleTypeDef* hdac)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (hdac->Instance == DAC1)
  {
  /* USER CODE BEGIN DAC1_MspInit 0 */
  /* USER CODE END DAC1_MspInit 0 */
    __HAL_RCC_DAC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN DAC1_MspInit 1 */
  /* USER CODE END DAC1_MspInit 1 */
  }
}

/* 函数说明：
 *   释放 DAC1 的 MSP 资源。
 * 输入：
 *   hdac: DAC 句柄指针。
 * 输出：
 *   无。
 * 作用：
 *   关闭 DAC1 时钟并释放对应 GPIO。
 */
void HAL_DAC_MspDeInit(DAC_HandleTypeDef* hdac)
{
  if (hdac->Instance == DAC1)
  {
  /* USER CODE BEGIN DAC1_MspDeInit 0 */
  /* USER CODE END DAC1_MspDeInit 0 */
    __HAL_RCC_DAC1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4 | GPIO_PIN_5);
  /* USER CODE BEGIN DAC1_MspDeInit 1 */
  /* USER CODE END DAC1_MspDeInit 1 */
  }
}

/* 函数说明：
 *   初始化 SPI1 的 MSP 资源。
 * 输入：
 *   hspi: SPI 句柄指针。
 * 输出：
 *   无。
 * 作用：
 *   为 SPI1 开启时钟并配置 SCK、MISO、MOSI 的复用功能。
 */
void HAL_SPI_MspInit(SPI_HandleTypeDef* hspi)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (hspi->Instance == SPI1)
  {
  /* USER CODE BEGIN SPI1_MspInit 0 */
  /* USER CODE END SPI1_MspInit 0 */
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN SPI1_MspInit 1 */
  /* USER CODE END SPI1_MspInit 1 */
  }
}

/* 函数说明：
 *   释放 SPI1 的 MSP 资源。
 * 输入：
 *   hspi: SPI 句柄指针。
 * 输出：
 *   无。
 * 作用：
 *   关闭 SPI1 时钟并释放对应 GPIO 复用资源。
 */
void HAL_SPI_MspDeInit(SPI_HandleTypeDef* hspi)
{
  if (hspi->Instance == SPI1)
  {
  /* USER CODE BEGIN SPI1_MspDeInit 0 */
  /* USER CODE END SPI1_MspDeInit 0 */
    __HAL_RCC_SPI1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);
  /* USER CODE BEGIN SPI1_MspDeInit 1 */
  /* USER CODE END SPI1_MspDeInit 1 */
  }
}

/* 函数说明：
 *   初始化 TIM6 的 MSP 资源。
 * 输入：
 *   htim_base: 定时器句柄指针。
 * 输出：
 *   无。
 * 作用：
 *   为 TIM6 开启时钟并打开对应 NVIC 中断。
 */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* htim_base)
{
  if (htim_base->Instance == TIM6)
  {
  /* USER CODE BEGIN TIM6_MspInit 0 */
  /* USER CODE END TIM6_MspInit 0 */
    __HAL_RCC_TIM6_CLK_ENABLE();
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
  /* USER CODE BEGIN TIM6_MspInit 1 */
  /* USER CODE END TIM6_MspInit 1 */
  }
}

/* 函数说明：
 *   释放 TIM6 的 MSP 资源。
 * 输入：
 *   htim_base: 定时器句柄指针。
 * 输出：
 *   无。
 * 作用：
 *   关闭 TIM6 时钟并撤销对应 NVIC 中断。
 */
void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* htim_base)
{
  if (htim_base->Instance == TIM6)
  {
  /* USER CODE BEGIN TIM6_MspDeInit 0 */
  /* USER CODE END TIM6_MspDeInit 0 */
    __HAL_RCC_TIM6_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
  /* USER CODE BEGIN TIM6_MspDeInit 1 */
  /* USER CODE END TIM6_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */

