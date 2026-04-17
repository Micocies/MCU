# V1.0 Fixed 100 Pixels Topology

V1.0 freezes the external imaging interface as a fixed `10 x 10` array.

## Frozen Constants

- `ARRAY_WIDTH = 10`
- `ARRAY_HEIGHT = 10`
- `PIXEL_COUNT = 100`
- `ADC_DEVICE_COUNT = 25`
- `ADC_CHANNELS_PER_DEVICE = 4`
- `SUBBOARD_COUNT = 5`
- `PIXELS_PER_SUBBOARD = 20`
- `LOGICAL_FRAME_RATE_HZ = 100`
- `FRAME_PERIOD_MS = 10`

## Pixel ID Rule

Pixels are assigned in row-major order:

```text
pixel_id = row * 10 + column
row      = pixel_id / 10
column   = pixel_id % 10
```

Examples:

- `(row=0, column=0) -> pixel_id=0`
- `(row=0, column=9) -> pixel_id=9`
- `(row=1, column=0) -> pixel_id=10`
- `(row=9, column=9) -> pixel_id=99`

## Hardware Route Rule

The V1.0 fixed mapping is:

```text
subboard_id = pixel_id / 20
adc_id      = (pixel_id / 4) % 5
adc_channel = (pixel_id % 20) % 4
```

This encodes the final target layout of `5 subboards x 5 ADS1220 x 4 channels`.

## V1.0 Active Signal

Only `PROJECT_ACTIVE_PIXEL_ID = 0` is backed by the current single ADS1220 signal path.
All other positions are filled by the frame builder according to the selected frame type:

- `FRAME_TYPE_TEST`: deterministic row/column test pattern
- `FRAME_TYPE_PARTIAL_REAL`: pixel `0` is real, the remaining 99 pixels use the test pattern
- `FRAME_TYPE_PLACEHOLDER`: all pixels are zero

This keeps the host-facing `100 pixels` contract stable before full 25-device hardware is connected.
