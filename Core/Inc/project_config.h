#ifndef __PROJECT_CONFIG_H
#define __PROJECT_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* V1.0 freezes the external imaging interface at 10 x 10 pixels. */
#define ARRAY_WIDTH              10U
#define ARRAY_HEIGHT             10U
#define PIXEL_COUNT              (ARRAY_WIDTH * ARRAY_HEIGHT)

#define ADC_DEVICE_COUNT         25U
#define ADC_CHANNELS_PER_DEVICE  4U

#define SUBBOARD_COUNT           5U
#define ADC_PER_SUBBOARD         5U
#define PIXELS_PER_SUBBOARD      20U

#define TARGET_PIXEL_RATE_HZ     100U
#define LOGICAL_FRAME_RATE_HZ    100U
#define FRAME_PERIOD_MS          10U

/* Only pixel 0 is backed by the current single ADS1220 signal path in V1.0. */
#define PROJECT_ACTIVE_PIXEL_ID  0U

#ifdef __cplusplus
}
#endif

#endif /* __PROJECT_CONFIG_H */
