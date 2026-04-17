#ifndef __FRAME_PROTOCOL_H
#define __FRAME_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "project_config.h"

#define FRAME_PROTOCOL_MAGIC           0xF100U
#define FRAME_PROTOCOL_VERSION         1U
#define FRAME_PROTOCOL_HEADER_BYTES    20U
#define FRAME_PROTOCOL_PAYLOAD_BYTES   (PIXEL_COUNT * sizeof(int32_t))

typedef enum
{
  FRAME_TYPE_TEST = 1,
  FRAME_TYPE_PARTIAL_REAL = 2,
  FRAME_TYPE_PLACEHOLDER = 3
} frame_type_t;

#if defined(__ICCARM__)
#define FRAME_PACKED_STRUCT typedef __packed struct
#define FRAME_PACKED_END(name) name
#else
#define FRAME_PACKED_STRUCT typedef struct __attribute__((packed))
#define FRAME_PACKED_END(name) name
#endif

FRAME_PACKED_STRUCT
{
  uint16_t magic;
  uint8_t version;
  uint8_t frame_type;
  uint32_t frame_id;
  uint16_t width;
  uint16_t height;
  uint32_t timestamp_us;
  uint16_t payload_bytes;
  uint16_t crc16;
} FRAME_PACKED_END(frame_header_t);

#undef FRAME_PACKED_STRUCT
#undef FRAME_PACKED_END

typedef struct
{
  frame_header_t header;
  int32_t pixels[PIXEL_COUNT];
} frame_packet_t;

typedef char frame_header_size_must_be_20[(sizeof(frame_header_t) == FRAME_PROTOCOL_HEADER_BYTES) ? 1 : -1];
typedef char frame_payload_size_must_be_400[(FRAME_PROTOCOL_PAYLOAD_BYTES == 400U) ? 1 : -1];
typedef char frame_packet_size_must_be_420[(sizeof(frame_packet_t) == (FRAME_PROTOCOL_HEADER_BYTES + FRAME_PROTOCOL_PAYLOAD_BYTES)) ? 1 : -1];

void frame_protocol_prepare_header(frame_header_t *header,
                                   frame_type_t frame_type,
                                   uint32_t frame_id,
                                   uint32_t timestamp_us);
uint16_t frame_protocol_crc16_ccitt(const uint8_t *data, uint32_t length, uint16_t seed);
uint16_t frame_protocol_compute_crc(const frame_packet_t *frame);
void frame_protocol_finalize(frame_packet_t *frame);
bool frame_protocol_validate(const frame_packet_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* __FRAME_PROTOCOL_H */
