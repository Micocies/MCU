#!/usr/bin/env python3
"""Decoder for the V1.0 fixed 10x10 MCU frame stream."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Iterable


FRAME_MAGIC = 0xF100
FRAME_VERSION = 1
ARRAY_WIDTH = 10
ARRAY_HEIGHT = 10
PIXEL_COUNT = ARRAY_WIDTH * ARRAY_HEIGHT
PAYLOAD_BYTES = PIXEL_COUNT * 4

FRAME_TYPE_TEST = 1
FRAME_TYPE_PARTIAL_REAL = 2
FRAME_TYPE_PLACEHOLDER = 3
FRAME_TYPE_FULL_REAL = 4

HEADER_STRUCT = struct.Struct("<HBBIHHIHH")
FRAME_BYTES = HEADER_STRUCT.size + PAYLOAD_BYTES
PIXEL_STRUCT = struct.Struct("<" + ("i" * PIXEL_COUNT))


@dataclass(frozen=True)
class Frame:
    frame_type: int
    frame_id: int
    width: int
    height: int
    timestamp_us: int
    pixels: tuple[int, ...]

    def rows(self) -> list[list[int]]:
        return [
            list(self.pixels[row * self.width : (row + 1) * self.width])
            for row in range(self.height)
        ]


class FrameDecodeError(ValueError):
    pass


def crc16_ccitt(data: bytes, seed: int = 0xFFFF) -> int:
    crc = seed
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _frame_crc(frame_bytes: bytes) -> int:
    header = bytearray(frame_bytes[: HEADER_STRUCT.size])
    header[-2] = 0
    header[-1] = 0
    return crc16_ccitt(frame_bytes[HEADER_STRUCT.size :], crc16_ccitt(bytes(header)))


def decode_frame(frame_bytes: bytes) -> Frame:
    if len(frame_bytes) != FRAME_BYTES:
        raise FrameDecodeError(f"expected {FRAME_BYTES} bytes, got {len(frame_bytes)}")

    (
        magic,
        version,
        frame_type,
        frame_id,
        width,
        height,
        timestamp_us,
        payload_bytes,
        crc16,
    ) = HEADER_STRUCT.unpack(frame_bytes[: HEADER_STRUCT.size])

    if magic != FRAME_MAGIC:
        raise FrameDecodeError(f"bad magic 0x{magic:04X}")
    if version != FRAME_VERSION:
        raise FrameDecodeError(f"bad version {version}")
    if width != ARRAY_WIDTH or height != ARRAY_HEIGHT:
        raise FrameDecodeError(f"bad shape {width}x{height}")
    if payload_bytes != PAYLOAD_BYTES:
        raise FrameDecodeError(f"bad payload size {payload_bytes}")

    actual_crc = _frame_crc(frame_bytes)
    if actual_crc != crc16:
        raise FrameDecodeError(f"bad crc 0x{crc16:04X}, expected 0x{actual_crc:04X}")

    pixels = PIXEL_STRUCT.unpack(frame_bytes[HEADER_STRUCT.size :])
    return Frame(frame_type, frame_id, width, height, timestamp_us, pixels)


def extract_frames(buffer: bytearray) -> Iterable[Frame]:
    sync = struct.pack("<H", FRAME_MAGIC)
    while len(buffer) >= FRAME_BYTES:
        sync_index = buffer.find(sync)
        if sync_index < 0:
            del buffer[:-1]
            return
        if sync_index > 0:
            del buffer[:sync_index]
            if len(buffer) < FRAME_BYTES:
                return

        candidate = bytes(buffer[:FRAME_BYTES])
        try:
            frame = decode_frame(candidate)
        except FrameDecodeError:
            del buffer[0]
            continue

        del buffer[:FRAME_BYTES]
        yield frame
