#ifndef __FRAME_BUILDER_H
#define __FRAME_BUILDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "frame_protocol.h"

typedef struct
{
  frame_type_t mode;
  int32_t last_real_sample;
} frame_builder_t;

void frame_builder_init(frame_builder_t *builder, frame_type_t mode);
void frame_builder_set_mode(frame_builder_t *builder, frame_type_t mode);
void frame_builder_build(frame_builder_t *builder,
                         frame_packet_t *frame,
                         uint32_t frame_id,
                         uint32_t timestamp_us,
                         int32_t real_sample);
void frame_builder_build_pixels(frame_builder_t *builder,
                                frame_packet_t *frame,
                                uint32_t frame_id,
                                uint32_t timestamp_us,
                                const int32_t pixels[PIXEL_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* __FRAME_BUILDER_H */
