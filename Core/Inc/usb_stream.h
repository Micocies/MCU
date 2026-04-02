#ifndef __USB_STREAM_H
#define __USB_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 发往上位机的固定长度二进制帧。
 * 保持 32 字节，便于两个样本刚好拼成一个 64 字节 FS 包。 */
typedef struct
{
  uint16_t magic;
  uint8_t version;
  uint8_t state;
  uint16_t flags;
  uint16_t reserved;
  uint32_t sequence;
  uint32_t timestamp_us;
  int32_t raw_code;
  int32_t filtered_code;
  int32_t baseline_code;
  int32_t corrected_code;
} sample_packet_t;

/* 编译期约束，防止结构体被编译器额外填充。 */
typedef char sample_packet_size_must_be_32[(sizeof(sample_packet_t) == 32U) ? 1 : -1];

typedef enum
{
  USB_STREAM_ENQUEUE_OK = 0,
  USB_STREAM_ENQUEUE_OK_DROPPED_OLDEST,
  USB_STREAM_ENQUEUE_ERR_INVALID_ARG
} usb_stream_enqueue_result_t;

/* 初始化 USB 发送队列。 */
void usb_stream_init(void);
/* 采样结果入队；当前实现仅支持单一主循环上下文访问。 */
usb_stream_enqueue_result_t usb_stream_enqueue(const sample_packet_t *pkt);
/* 尝试把队列头部数据送往 USB CDC。 */
void usb_stream_service(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_STREAM_H */

