/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v3.0_Cube
  * @brief          : Usb device for Virtual Com Port.
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
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */
#include "app.h"
#include "usb_stream.h"
/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/
/* USER CODE END PV */

uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

extern USBD_HandleTypeDef hUsbDeviceFS;

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS,
  CDC_TransmitCplt_FS
};

/* 函数说明：
 *   初始化 USB CDC 接口。
 * 输入：
 *   无。
 * 输出：
 *   返回 CDC 初始化状态。
 * 作用：
 *   为 USB CDC 收发配置默认缓冲区。
 */
static int8_t CDC_Init_FS(void)
{
  /* 当前工程只主动发送，不依赖上位机下发控制命令。 */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
  return (USBD_OK);
}

/* 函数说明：
 *   去初始化 USB CDC 接口。
 * 输入：
 *   无。
 * 输出：
 *   返回 CDC 去初始化状态。
 * 作用：
 *   预留 CDC 去初始化接口，当前实现不做额外资源释放。
 */
static int8_t CDC_DeInit_FS(void)
{
  return (USBD_OK);
}

/* 函数说明：
 *   处理 CDC 类控制命令。
 * 输入：
 *   cmd: CDC 类命令。
 *   pbuf: 命令数据缓冲区。
 *   length: 数据长度。
 * 输出：
 *   返回命令处理状态。
 * 作用：
 *   当前大多保持默认空实现，保留后续扩展上位机控制命令的入口。
 */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  UNUSED(pbuf);
  UNUSED(length);

  switch (cmd)
  {
    case CDC_SEND_ENCAPSULATED_COMMAND:
    case CDC_GET_ENCAPSULATED_RESPONSE:
    case CDC_SET_COMM_FEATURE:
    case CDC_GET_COMM_FEATURE:
    case CDC_CLEAR_COMM_FEATURE:
    case CDC_SET_LINE_CODING:
    case CDC_GET_LINE_CODING:
    case CDC_SET_CONTROL_LINE_STATE:
    case CDC_SEND_BREAK:
    default:
      break;
  }

  return (USBD_OK);
}

/* 函数说明：
 *   处理 USB CDC 接收事件。
 * 输入：
 *   Buf: 接收数据缓冲区。
 *   Len: 接收长度指针。
 * 输出：
 *   返回接收处理状态。
 * 作用：
 *   解析最小命令字节并重新挂载接收缓冲区。
 */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  if ((Buf == NULL) || (Len == NULL))
  {
    return (USBD_FAIL);
  }

  if (*Len != 0U)
  {
    app_on_usb_command_rx(Buf, *Len);
  }

  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
}

/* 函数说明：
 *   发送一包 USB CDC 数据。
 * 输入：
 *   Buf: 待发送数据缓冲区。
 *   Len: 待发送字节数。
 * 输出：
 *   返回发送状态，可能为 OK、FAIL 或 BUSY。
 * 作用：
 *   若端点忙则立即返回 BUSY，由上层发送队列稍后重试。
 */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;

  /* 未枚举、空指针或零长度时直接返回忙，交给上层发送队列稍后重试。 */
  if ((hcdc == NULL) || (Buf == NULL) || (Len == 0U))
  {
    return USBD_BUSY;
  }

  /* USB 端点仍在发送上一包时，不阻塞，交给上层保留队列。 */
  if (hcdc->TxState != 0U)
  {
    return USBD_BUSY;
  }

  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
  return result;
}

/* 函数说明：
 *   USB CDC 发送完成回调。
 * 输入：
 *   pbuf: 已发送数据缓冲区。
 *   Len: 已发送长度指针。
 *   epnum: 端点号。
 * 输出：
 *   返回回调处理状态。
 * 作用：
 *   通知 USB stream 当前异步发送完成，不在回调中继续发下一包。
 */
static int8_t CDC_TransmitCplt_FS(uint8_t *pbuf, uint32_t *Len, uint8_t epnum)
{
  uint8_t result = USBD_OK;

  UNUSED(pbuf);
  UNUSED(Len);
  UNUSED(epnum);

  usb_stream_on_tx_complete();
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

