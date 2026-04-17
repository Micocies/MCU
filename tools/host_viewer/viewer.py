#!/usr/bin/env python3
"""Minimal V1.0 USB CDC viewer for the fixed 10x10 frame stream."""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required: pip install pyserial") from exc

try:
    import matplotlib.pyplot as plt
except ImportError as exc:  # pragma: no cover
    raise SystemExit("matplotlib is required: pip install matplotlib") from exc

from frame_decoder import ARRAY_HEIGHT, ARRAY_WIDTH, extract_frames


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Display MCU V1.0 fixed 10x10 frames.")
    parser.add_argument("port", help="Serial port, for example COM6")
    parser.add_argument("--baudrate", type=int, default=115200, help="CDC open baudrate")
    parser.add_argument("--timeout", type=float, default=0.2, help="serial read timeout")
    parser.add_argument("--max-frames", type=int, default=0, help="stop after N frames, 0 means run forever")
    return parser


def update_image(image, title, frame) -> None:
    rows = frame.rows()
    vmin = min(frame.pixels)
    vmax = max(frame.pixels)
    image.set_data(rows)
    image.set_clim(vmin, vmax if vmax != vmin else vmin + 1)
    title.set_text(f"frame={frame.frame_id} type={frame.frame_type} t={frame.timestamp_us} us")
    plt.pause(0.001)


def run_viewer(port: str, baudrate: int, timeout: float, max_frames: int) -> int:
    buffer = bytearray()
    shown = 0

    plt.ion()
    figure, axis = plt.subplots()
    image = axis.imshow([[0] * ARRAY_WIDTH for _ in range(ARRAY_HEIGHT)], cmap="viridis", interpolation="nearest")
    title = axis.set_title("waiting for frames")
    axis.set_xticks(range(ARRAY_WIDTH))
    axis.set_yticks(range(ARRAY_HEIGHT))
    figure.colorbar(image, ax=axis)

    with serial.Serial(port, baudrate, timeout=timeout) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        while max_frames == 0 or shown < max_frames:
            chunk = ser.read(1024)
            if chunk:
                buffer.extend(chunk)

            for frame in extract_frames(buffer):
                update_image(image, title, frame)
                shown += 1
                if max_frames != 0 and shown >= max_frames:
                    break

            if not plt.fignum_exists(figure.number):
                break

    return 0


def main() -> int:
    args = build_arg_parser().parse_args()
    return run_viewer(args.port, args.baudrate, args.timeout, args.max_frames)


if __name__ == "__main__":
    sys.exit(main())
