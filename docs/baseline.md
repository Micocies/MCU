# V0.1 Baseline

## Scope

`v0.1.0` freezes the single-ADS1220 baseline around one rule: keep the sampling loop simple and observable.

Included in this baseline:

- One ADS1220 default register set
- `1 kHz` single-channel polling loop driven by `TIM6`
- Fixed `32-byte` USB frame format
- Minimal USB capture and continuity-check script
- Firmware version, build number, and frozen-parameter signature export

Explicitly excluded from this baseline:

- Multi-ADC
- Pixel mapping
- DMA
- Host-side image display

## Frozen Defaults

Firmware identity:

- Firmware version: `v0.1.0`
- Build number: `1`
- USB packet version: `2`

Sampling and timeout defaults:

- `APP_SAMPLE_RATE_HZ = 1000`
- `APP_BIAS_STABILIZE_MS = 100`
- `APP_DARK_CALIBRATION_SAMPLES = 256`
- `APP_DRDY_TIMEOUT_MS = 10`
- `APP_FILTER_ALPHA_SHIFT = 3`
- `APP_USB_QUEUE_DEPTH = 16`

Analog defaults:

- `APP_DAC_BIAS_CH1 = 2048`
- `APP_DAC_BIAS_CH2 = 2048`
- ADS1220 `CONFIG0 = 0x08`
- ADS1220 `CONFIG1 = 0x04`
- ADS1220 `CONFIG2 = 0x10`
- ADS1220 `CONFIG3 = 0x00`

Frozen-parameter fingerprint:

- `param_signature = FNV-1a(packet_version + sample timing + queue depth + DAC bias + ADS1220 defaults)`
- Host can compare the signature against the startup `INFO` frame to confirm that factory defaults are intact.

## USB Baseline Format

All outbound USB packets remain fixed at `32 bytes`:

```c
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
```

Rules:

- `magic` is always `0xA55A`
- `version` is always `2` for this baseline
- Normal sample frames use `sequence` as the sample-only monotonic counter
- Fault and metadata frames use an independent sequence domain and must not be included in sample continuity checks
- `reserved` carries `adc_status | (usb_status << 8)` for sample/fault frames
- `reserved` carries metadata subtype for `INFO/PARAM` frames

Frame flags:

- `0x0001` `DRDY_TIMEOUT`
- `0x0002` `SPI_ERROR`
- `0x0004` `COMM_CHECK_FAILED`
- `0x0008` `USB_OVERFLOW`
- `0x0010` `FAULT_STATE`
- `0x0020` `FAULT_REPORT`
- `0x0040` `INFO_FRAME`
- `0x0080` `PARAM_FRAME`

Metadata frame payload mapping:

- `INFO_FRAME`, subtype `0`
  `raw_code=semver_packed`, `filtered_code=build_number`, `baseline_code=packet_version`, `corrected_code=param_signature`
- `PARAM_FRAME`, subtype `1`
  `raw_code=sample_rate_hz`, `filtered_code=bias_stabilize_ms`, `baseline_code=dark_calibration_samples`, `corrected_code=drdy_timeout_ms`
- `PARAM_FRAME`, subtype `2`
  `raw_code=filter_alpha_shift`, `filtered_code=usb_queue_depth`, `baseline_code=dac_bias_ch2<<16|dac_bias_ch1`, `corrected_code=ads1220_default_config`

## Host Commands

Firmware recognizes a minimal CDC command set. Commands can be sent as a single byte or as part of a text line; only the key byte is used.

- `I` or `i`: re-send the `INFO` frame
- `P` or `p`: re-send both `PARAM` frames
- `B` or `b`: re-send the full baseline descriptor set

The firmware also queues one baseline descriptor set automatically during startup.

## Minimal Validation Script

`tools/usb_capture.py` performs:

- USB frame alignment on `0xA55A`
- Packet version verification
- Sample-sequence continuity checks
- Metadata decoding
- Fault frame summary printing

Example:

```bash
python tools/usb_capture.py COM6 --request all --max-frames 100000 --timeout 120
```

Exit code:

- `0`: no parse error, no sample sequence gap observed in the capture window
- `1`: parse error or sample sequence discontinuity observed

## Baseline Test Report Template

Test target:

- Board:
- Firmware: `v0.1.0`
- Build:
- Host script version: `tools/usb_capture.py`
- Capture port:

Checklist:

- [ ] Power-on startup test
- [ ] Reset recovery test
- [ ] DRDY timeout test
- [ ] USB unplug/replug test
- [ ] Dark calibration repeatability test

Exit criteria:

- [ ] Continuous run for `8 h` without hang
- [ ] No random `FAULT`
- [ ] `100000` USB packets captured without parse error
- [ ] Sample sequence increments continuously
- [ ] Default parameters identifiable through metadata frames

Observed results:

- Startup behavior:
- Fault behavior:
- Sequence continuity:
- Calibration repeatability:
- Notes / open risks:
