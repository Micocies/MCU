#!/usr/bin/env python3
"""Minimal USB CDC capture helper for the STM32G431 single-ADS1220 baseline."""

from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass

try:
    import serial
except ImportError as exc:  # pragma: no cover - import guard
    raise SystemExit("pyserial is required: pip install pyserial") from exc


PACKET_STRUCT = struct.Struct("<HBBHHIIiiii")
PACKET_MAGIC = 0xA55A
PACKET_VERSION = 2

FLAG_DRDY_TIMEOUT = 0x0001
FLAG_SPI_ERROR = 0x0002
FLAG_COMM_CHECK_FAILED = 0x0004
FLAG_USB_OVERFLOW = 0x0008
FLAG_FAULT_STATE = 0x0010
FLAG_FAULT_REPORT = 0x0020
FLAG_INFO_FRAME = 0x0040
FLAG_PARAM_FRAME = 0x0080

META_SUBTYPE_INFO = 0
META_SUBTYPE_PARAMS_TIMING = 1
META_SUBTYPE_PARAMS_ADC = 2

CMD_INFO = b"I"
CMD_PARAMS = b"P"
CMD_BASELINE = b"B"


@dataclass
class Packet:
    magic: int
    version: int
    state: int
    flags: int
    reserved: int
    sequence: int
    timestamp_us: int
    raw_code: int
    filtered_code: int
    baseline_code: int
    corrected_code: int

    @classmethod
    def from_bytes(cls, chunk: bytes) -> "Packet":
        return cls(*PACKET_STRUCT.unpack(chunk))

    def is_info(self) -> bool:
        return (self.flags & FLAG_INFO_FRAME) != 0

    def is_param(self) -> bool:
        return (self.flags & FLAG_PARAM_FRAME) != 0

    def is_fault(self) -> bool:
        return (self.flags & FLAG_FAULT_REPORT) != 0

    def is_sample(self) -> bool:
        return not self.is_info() and not self.is_param() and not self.is_fault()


def u32(value: int) -> int:
    return value & 0xFFFFFFFF


def decode_semver(value: int) -> str:
    value = u32(value)
    major = (value >> 16) & 0xFF
    minor = (value >> 8) & 0xFF
    patch = value & 0xFF
    return f"{major}.{minor}.{patch}"


def decode_flags(flags: int) -> str:
    names = []
    if flags & FLAG_DRDY_TIMEOUT:
        names.append("DRDY_TIMEOUT")
    if flags & FLAG_SPI_ERROR:
        names.append("SPI_ERROR")
    if flags & FLAG_COMM_CHECK_FAILED:
        names.append("COMM_CHECK_FAILED")
    if flags & FLAG_USB_OVERFLOW:
        names.append("USB_OVERFLOW")
    if flags & FLAG_FAULT_STATE:
        names.append("FAULT_STATE")
    if flags & FLAG_FAULT_REPORT:
        names.append("FAULT_REPORT")
    if flags & FLAG_INFO_FRAME:
        names.append("INFO")
    if flags & FLAG_PARAM_FRAME:
        names.append("PARAM")
    return ",".join(names) if names else "NONE"


def print_info_packet(packet: Packet) -> None:
    print(
        "[info] "
        f"fw=v{decode_semver(packet.raw_code)} "
        f"build={u32(packet.filtered_code)} "
        f"packet={u32(packet.baseline_code)} "
        f"param_sig=0x{u32(packet.corrected_code):08X}"
    )


def print_param_packet(packet: Packet) -> None:
    if packet.reserved == META_SUBTYPE_PARAMS_TIMING:
        print(
            "[param] "
            f"sample_rate_hz={u32(packet.raw_code)} "
            f"bias_stabilize_ms={u32(packet.filtered_code)} "
            f"dark_cal_samples={u32(packet.baseline_code)} "
            f"drdy_timeout_ms={u32(packet.corrected_code)}"
        )
        return

    if packet.reserved == META_SUBTYPE_PARAMS_ADC:
        dac_bias = u32(packet.baseline_code)
        dac_ch1 = dac_bias & 0xFFFF
        dac_ch2 = (dac_bias >> 16) & 0xFFFF
        print(
            "[param] "
            f"filter_alpha_shift={u32(packet.raw_code)} "
            f"usb_queue_depth={u32(packet.filtered_code)} "
            f"dac_bias_ch1={dac_ch1} "
            f"dac_bias_ch2={dac_ch2} "
            f"ads1220_default_cfg=0x{u32(packet.corrected_code):08X}"
        )
        return

    print(
        "[param] "
        f"subtype={packet.reserved} raw={packet.raw_code} "
        f"filtered={packet.filtered_code} baseline={packet.baseline_code} "
        f"corrected={packet.corrected_code}"
    )


def request_metadata(ser: serial.Serial, mode: str) -> None:
    if mode == "none":
        return
    if mode == "info":
        ser.write(CMD_INFO)
    elif mode == "params":
        ser.write(CMD_PARAMS)
    else:
        ser.write(CMD_BASELINE)


def read_packets(ser: serial.Serial, max_frames: int, timeout_s: float) -> int:
    start = time.time()
    buffer = bytearray()
    total_frames = 0
    parse_errors = 0
    sample_frames = 0
    last_sample_sequence: int | None = None
    sequence_gaps = 0

    while total_frames < max_frames:
        if timeout_s > 0 and (time.time() - start) >= timeout_s:
            print(f"[stop] timeout after {timeout_s:.1f}s")
            break

        chunk = ser.read(256)
        if not chunk:
            continue
        buffer.extend(chunk)

        while len(buffer) >= PACKET_STRUCT.size and total_frames < max_frames:
            sync_index = buffer.find(b"\x5A\xA5")
            if sync_index < 0:
                parse_errors += len(buffer)
                buffer.clear()
                break
            if sync_index > 0:
                parse_errors += sync_index
                del buffer[:sync_index]
                if len(buffer) < PACKET_STRUCT.size:
                    break

            frame = bytes(buffer[: PACKET_STRUCT.size])
            packet = Packet.from_bytes(frame)
            if packet.magic != PACKET_MAGIC or packet.version != PACKET_VERSION:
                parse_errors += 1
                del buffer[0]
                continue

            del buffer[: PACKET_STRUCT.size]
            total_frames += 1

            if packet.is_info():
                print_info_packet(packet)
                continue

            if packet.is_param():
                print_param_packet(packet)
                continue

            if packet.is_fault():
                print(
                    "[fault] "
                    f"seq={packet.sequence} state={packet.state} "
                    f"flags={decode_flags(packet.flags)} "
                    f"adc_status=0x{packet.reserved & 0xFF:02X} "
                    f"usb_status=0x{(packet.reserved >> 8) & 0xFF:02X}"
                )
                continue

            sample_frames += 1
            if last_sample_sequence is not None and packet.sequence != (last_sample_sequence + 1):
                sequence_gaps += 1
                print(
                    "[warn] "
                    f"sample sequence jump {last_sample_sequence} -> {packet.sequence}"
                )
            last_sample_sequence = packet.sequence

    print(
        "[summary] "
        f"frames={total_frames} samples={sample_frames} "
        f"parse_errors={parse_errors} sample_sequence_gaps={sequence_gaps}"
    )
    return 1 if (parse_errors != 0 or sequence_gaps != 0) else 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Capture and validate fixed 32-byte USB packets from the baseline firmware."
    )
    parser.add_argument("port", help="Serial port, for example COM6")
    parser.add_argument("--baudrate", type=int, default=115200, help="CDC open baudrate")
    parser.add_argument(
        "--request",
        choices=("none", "info", "params", "all"),
        default="all",
        help="Send a minimal metadata request command after opening the port",
    )
    parser.add_argument(
        "--max-frames",
        type=int,
        default=1000,
        help="Stop after receiving this many valid 32-byte packets",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="Overall capture timeout in seconds, 0 means wait forever",
    )
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()
    with serial.Serial(args.port, args.baudrate, timeout=0.2) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        request_metadata(ser, args.request)
        return read_packets(ser, args.max_frames, args.timeout)


if __name__ == "__main__":
    sys.exit(main())
