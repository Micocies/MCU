# Continuous ADS1220 Sampling

This document records the sampling timing after the continuous-conversion refactor.

## Timing Contract

- ADS1220 is configured as turbo mode, 2000 SPS, continuous conversion.
- Firmware sends `START/SYNC` only when the continuous chain is started after init or recovery.
- During normal run, `DRDY` means a new ADC result is ready. The main loop reads the latest 24-bit sample after consuming a DRDY event.
- `TIM6` no longer starts ADC conversions. It only provides the 1 kHz output permit.
- ISR code only counts events:
  - `EXTI0`: increments `pending_drdy_count`
  - `TIM6`: increments `pending_output_ticks`

## State Responsibilities

- `COMM_CHECK`: starts continuous conversion once, then waits for the first DRDY sample and validates register readback.
- `DARK_CALIBRATE`: keeps continuous conversion running and accumulates calibration samples from DRDY-driven reads.
- `WAIT_DRDY`: waits for continuous ADC sample readiness, with `APP_DRDY_TIMEOUT_MS` interpreted as "continuous was started but no DRDY arrived".
- `WAIT_TRIGGER`: gates output using TIM6 permits. It never calls `adc_protocol_start_conversion()`.
- `USB_FLUSH`: consumes one accepted 1 kHz output sample. V1.0 still publishes one 10x10 image frame every 10 accepted output samples.
- `RECOVER` and `FAULT`: stop continuous conversion before recovery or hold. Successful recovery returns to startup self-check, which starts continuous conversion once again.

## Downsampling

The ADC runs at 2 kSPS while the output sample cadence is 1 kHz.

The firmware uses explicit DRDY-count decimation:

```text
2 DRDY events -> 1 latest output-sample candidate
candidate + 1 TIM6 output permit -> USB output path
```

If the main loop is delayed and multiple DRDY edges are counted before it can read SPI, the event count is preserved for decimation, but ADS1220 can only provide the latest data register value. This avoids silently losing timing information while keeping SPI reads out of interrupt context.

