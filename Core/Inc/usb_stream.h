#ifndef __USB_STREAM_H
#define __USB_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 发往上位机的固定长度二进制帧。
 * 保持 32 字节，便于两个数据帧刚好拼成一个 64 字节 FS 包。
 * 默认承载单样本；若 flags 置位 INFO/PARAM，则 4 个 int32 字段改作元信息负载。 */
typedef struct
{
  uint16_t magic;     // 固定帧头，便于上位机快速识别帧边界和版本
  uint8_t version;    // 协议版本，便于后续升级时兼容处理
  uint8_t state;      // 预留状态位，当前未定义具体含义
  uint16_t flags;     // 标志位，指示样本状态或帧类型（如信息帧、参数帧等）
  uint16_t reserved;  // 预留对齐字节，保持结构体大小为 32 字节
  uint32_t sequence;  // 样本序号，主样本队列递增，辅助队列保持与主队列一致以便关联分析
  uint32_t timestamp_us;// 采样时刻相对于某个起始点的微秒级时间戳，便于上位机重建采样时序
  int32_t raw_code;     // 原始 ADC 码值，24-bit 数据右对齐存储在 32-bit 字段中
  int32_t filtered_code;// 滤波后 ADC 码值，24-bit 数据右对齐存储在 32-bit 字段中
  int32_t baseline_code;// 基线 ADC 码值，24-bit 数据右对齐存储在 32-bit 字段中
  int32_t corrected_code;// 校正后 ADC 码值，24-bit 数据右对齐存储在 32-bit 字段中
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
/* 采样结果入队；样本队列满时仅影响样本帧。 */
usb_stream_enqueue_result_t usb_stream_enqueue_sample(const sample_packet_t *pkt);
/* 元信息/故障结果入队；辅助队列与样本队列隔离，避免打断样本连续性。 */
usb_stream_enqueue_result_t usb_stream_enqueue_aux(const sample_packet_t *pkt);
/* 尝试把队列头部数据送往 USB CDC。 */
void usb_stream_service(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_STREAM_H */

