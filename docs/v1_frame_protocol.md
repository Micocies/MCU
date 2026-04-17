# V1.0 Frame Protocol

V1.0 changes the primary USB data stream from single-sample packets to fixed full-frame image packets.

## Byte Order

All fields are little-endian.

## Header

```c
typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  frame_type;
    uint32_t frame_id;
    uint16_t width;
    uint16_t height;
    uint32_t timestamp_us;
    uint16_t payload_bytes;
    uint16_t crc16;
} frame_header_t;
```

Header size is fixed at `20 bytes`.

Field values:

- `magic = 0xF100`
- `version = 1`
- `frame_type = 1`: deterministic test frame
- `frame_type = 2`: partial-real frame, currently pixel `0` real and the rest test pattern
- `frame_type = 3`: placeholder frame
- `width = 10`
- `height = 10`
- `payload_bytes = 400`

## Payload

The payload is always:

```c
int32_t pixels[100];
```

The payload is row-major:

```text
pixels[pixel_id] = pixels[row * 10 + column]
```

Total frame size is:

```text
20 byte header + 400 byte payload = 420 bytes
```

## CRC

`crc16` uses CRC-16/CCITT with:

- polynomial `0x1021`
- initial value `0xFFFF`
- no final xor

The CRC input is:

1. Header bytes with `crc16` treated as `0x0000`
2. The 400-byte payload

## Host Viewer

Use:

```powershell
python tools\host_viewer\viewer.py COM6 --max-frames 1000
```

The decoder module is `tools/host_viewer/frame_decoder.py`.
It searches the stream for `0xF100`, validates fixed shape and CRC, then returns a `10 x 10` frame.

## Compatibility Note

The older V0.x 32-byte `sample_packet_t` format is still used internally for auxiliary metadata and fault reports.
Normal image data is now sent as `frame_packet_t`.
