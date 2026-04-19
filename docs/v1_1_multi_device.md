# V1.1 fixed 25-device ADS1220 polling

## Hardware addressing model

V1.1 fixes the acquisition topology at 25 ADS1220 devices:

- 5 subboards
- 5 ADS1220 devices per subboard
- 4 single-ended input channels per ADS1220
- 100 pixels total

The pixel mapping is fixed:

```text
pixel_id = device_id * 4 + channel_id
```

The MCU address lines are used as follows:

- `SUB_SEL_A0/A1/A2` select the active subboard.
- `ADC_SEL_A0/A1/A2` select the ADS1220 on that subboard.
- `ADC_DRDY_MUX` is only the DRDY signal for the currently selected ADS1220.
- `ADC_RST_ALL` resets all ADS1220 devices together.
- `ADC_START_ALL` can pulse all ADS1220 START pins together.
- `ADC_CS_GATE` gates the SPI chip-select transaction through the external decoder.

`ADC_CS_GATE` is connected to the 74HC138 `E1` input. `E1` is active low and ADS1220 `CS` is active low, so the software polarity is:

- `adc_bus_cs_assert()` drives `ADC_CS_GATE` low.
- `adc_bus_cs_deassert()` drives `ADC_CS_GATE` high.

The idle state is always deasserted, so `ADC_CS_GATE` must remain high when no SPI transaction is active. Upper layers must not write `ADC_CS_GATE` directly; any future polarity change belongs only in `adc_bus.c`.

## Software layers

`board_topology`

Owns the fixed topology. It exposes `g_ads1220_routes[25]`, route lookup by `device_id`, and the fixed `pixel_id = device_id * 4 + channel_id` mapping.

`adc_bus`

Owns all MCU GPIO operations for ADS1220 routing, shared reset/start, DRDY readback, CS gate polarity, and SPI transport. Protocol/device/scheduler/app code do not directly manipulate the ADS1220 GPIO macros.

`adc_protocol`

Owns ADS1220 SPI commands, register read/write, raw 24-bit parsing, and link-check register comparison. It operates on the device already selected by `adc_bus` and no longer stores a single global expected configuration mirror.

`ads1220_device`

Owns the fixed table of 25 device objects. Each object stores an independent `expected_config`, last raw code, last protocol status, and route identity.

`ads1220_scheduler`

Owns the single-active-device polling state machine:

- `SELECT_DEVICE`
- `ENSURE_CONFIG`
- `START_OR_ARM`
- `WAIT_DRDY`
- `READ_SAMPLE`
- `STORE_RESULT`
- `ADVANCE_DEVICE`

Only the current selected device is serviced. `WAIT_DRDY` uses either the app-provided pending DRDY event or `adc_bus_is_selected_drdy_low()`.

When the scheduler selects a new device/channel it requests the app to clear stale DRDY state before the next START/WAIT window. The app also tags EXTI DRDY events with the scheduler's current `device_id` and `channel_id`, so a pending event from the previous selected ADS1220 cannot be consumed by the next device.

`app`

Owns the USB/output tick and scheduler/service tick. It keeps the 100-pixel buffer and uses the existing `frame_builder` and `usb_stream` path to send the unchanged external frame format.

Scheduler errors are routed back through the existing diagnostic and recovery policy:

- DRDY timeout enters `APP_STATE_RECOVER` and uses ADC reconfigure recovery.
- SPI timeout uses SPI retry recovery first.
- SPI/config errors use ADC reset/reconfigure/link-check recovery.
- Repeated recovery failure enters `APP_STATE_FAULT`.

FAULT reports are throttled by `APP_FAULT_REPORT_INTERVAL_MS`.

## Current limits

- Fixed 25 ADS1220 devices only.
- No arbitrary-N topology framework.
- No DMA.
- No multiple-DRDY concurrent interrupt model.
- One shared SPI bus.
- One active selected ADS1220 at a time.
- `ADC_DRDY_MUX` only describes the currently selected ADS1220.
- V1.1 normal image output uses `FRAME_TYPE_FULL_REAL` while retaining the same 420-byte frame layout.
