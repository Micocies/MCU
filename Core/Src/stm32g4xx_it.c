/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32g4xx_it.c
  * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "app.h"
#include "stm32g4xx_it.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */
/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern PCD_HandleTypeDef hpcd_USB_FS;
extern TIM_HandleTypeDef htim6;
/* USER CODE BEGIN EV */
/* USER CODE END EV */

/* 函数说明：
 *   Non-maskable 中断入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   当前实现直接停留在死循环，便于调试异常现场。
 */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */
  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
  while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/* 函数说明：
 *   HardFault 中断入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   当前实现直接停留在死循环，便于调试异常现场。
 */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/* 函数说明：
 *   MemManage 中断入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   当前实现直接停留在死循环，便于调试异常现场。
 */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/* 函数说明：
 *   BusFault 中断入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   当前实现直接停留在死循环，便于调试异常现场。
 */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/* 函数说明：
 *   UsageFault 中断入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   当前实现直接停留在死循环，便于调试异常现场。
 */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/* 函数说明：
 *   SVC 中断入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   当前未附加自定义逻辑，保留默认空实现。
 */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */
  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */
  /* USER CODE END SVCall_IRQn 1 */
}

/* 函数说明：
 *   DebugMon 中断入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   当前未附加自定义逻辑，保留默认空实现。
 */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */
  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */
  /* USER CODE END DebugMonitor_IRQn 1 */
}

/* 函数说明：
 *   PendSV 中断入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   当前未附加自定义逻辑，保留默认空实现。
 */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */
  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */
  /* USER CODE END PendSV_IRQn 1 */
}

/* 函数说明：
 *   SysTick 中断入口。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   维护 HAL 的系统节拍。
 */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */
  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */
  /* USER CODE END SysTick_IRQn 1 */
}

/* 函数说明：
 *   处理 ADS1220 的 DRDY 外部中断。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   只清中断并上报数据就绪事件，不在 ISR 中做 SPI 阻塞读。
 */
void EXTI0_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI0_IRQn 0 */
  /* USER CODE END EXTI0_IRQn 0 */
  if (__HAL_GPIO_EXTI_GET_IT(ADC_DRDY_Pin) != RESET)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(ADC_DRDY_Pin);
    app_on_drdy_isr();
  }
  /* USER CODE BEGIN EXTI0_IRQn 1 */
  /* USER CODE END EXTI0_IRQn 1 */
}

/* 函数说明：
 *   处理 TIM6 更新中断。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   只生成采样节拍事件，不在中断里直接发起 SPI 或 USB 操作。
 */
void TIM6_DAC_IRQHandler(void)
{
  /* USER CODE BEGIN TIM6_DAC_IRQn 0 */
  /* USER CODE END TIM6_DAC_IRQn 0 */
  if (__HAL_TIM_GET_FLAG(&htim6, TIM_FLAG_UPDATE) != RESET)
  {
    if (__HAL_TIM_GET_IT_SOURCE(&htim6, TIM_IT_UPDATE) != RESET)
    {
      __HAL_TIM_CLEAR_IT(&htim6, TIM_IT_UPDATE);
      app_on_sample_tick_isr();
    }
  }
  /* USER CODE BEGIN TIM6_DAC_IRQn 1 */
  /* USER CODE END TIM6_DAC_IRQn 1 */
}

/* 函数说明：
 *   处理 USB 低优先级中断。
 * 输入：
 *   无。
 * 输出：
 *   无。
 * 作用：
 *   将 USB 中断转发给 Cube USB 设备栈处理。
 */
void USB_LP_IRQHandler(void)
{
  /* USER CODE BEGIN USB_LP_IRQn 0 */
  /* USER CODE END USB_LP_IRQn 0 */
  HAL_PCD_IRQHandler(&hpcd_USB_FS);
  /* USER CODE BEGIN USB_LP_IRQn 1 */
  /* USER CODE END USB_LP_IRQn 1 */
}

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */

