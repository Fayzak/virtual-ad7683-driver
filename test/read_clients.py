#!/usr/bin/env python3
"""Test sysfs, ioctl, reads, and /proc/ad7683. Run with no other driver clients."""

import argparse
import errno
import fcntl
import os
from pathlib import Path
import signal
import struct
import sys

GET_RATE = (2 << 30) | (4 << 16) | (ord("A") << 8)
SET_RATE = (1 << 30) | (4 << 16) | (ord("A") << 8) | 1
CLEAR_BUFFER = (ord("A") << 8) | 2


def check(name, actual, expected):
    if actual != expected:
        raise RuntimeError(f"{name}: expected {expected}, got {actual}")


def expect_error(name, expected_errno, operation, *args):
    try:
        operation(*args)
    except OSError as exc:
        check(name, exc.errno, expected_errno)
        return
    raise RuntimeError(f"{name}: expected errno={expected_errno}, but the operation succeeded")


def passed(message):
    print(f"[PASS] {message}", flush=True)


def get_rate(fd):
    value = bytearray(4)
    fcntl.ioctl(fd, GET_RATE, value)
    return struct.unpack("=I", value)[0]


def set_rate(fd, rate):
    fcntl.ioctl(fd, SET_RATE, struct.pack("=I", rate))


def read_proc(path):
    result = {"rows": []}
    row = None
    for line in Path(path).read_text().splitlines():
        if not line.strip():
            continue
        if line.startswith("client "):
            row = {}
            result["rows"].append(row)
            continue
        key, value = line.strip().split(":", 1)
        target = row if line.startswith(" ") else result
        target[key] = int(value)
    required = {"sample_rate_hz", "samples_generated", "clients"}
    if not required <= result.keys():
        raise RuntimeError("Incomplete /proc header")
    for row in result["rows"]:
        if set(row) != {"capacity", "buffered", "samples_read", "overruns"}:
            raise RuntimeError("Incomplete client statistics")
    return result


def check_proc(path, expected_clients, rate=None):
    stats = read_proc(path)
    check("/proc: client count", stats["clients"], expected_clients)
    check("/proc: client row count", len(stats["rows"]), expected_clients)
    if rate is not None:
        check("/proc: sample rate", stats["sample_rate_hz"], rate)
    for index, row in enumerate(stats["rows"]):
        valid = (row["capacity"] > 0 and 0 <= row["buffered"] <= row["capacity"]
                 and row["samples_read"] >= 0 and row["overruns"] >= 0)
        check(f"/proc: counter bounds for client {index}", valid, True)
    return stats


def timeout_handler(signum, frame):
    raise TimeoutError("Read did not complete within 5 seconds")


def read_samples(fd):
    signal.alarm(5)
    try:
        return os.read(fd, 32)
    finally:
        signal.alarm(0)


def check_rate_interfaces(fds, args, expected):
    for index, fd in enumerate(fds):
        check(f"ioctl: sample rate for client {index}", get_rate(fd), expected)
    path = Path(args.sysfs) / "sample_rate"
    check("sysfs: sample rate", int(path.read_text()), expected)
    check_proc(args.proc, 2, expected)


def test_sysfs(fds, args):
    path = Path(args.sysfs) / "sample_rate"
    check("sysfs: initial sample rate", int(path.read_text()), get_rate(fds[0]))
    for rate in (1, 100000, 100):
        path.write_text(f"{rate}\n")
        check_rate_interfaces(fds, args, rate)
    for value in ("0", "100001", "abc", "-1"):
        expect_error(f"sysfs({value})", errno.EINVAL, path.write_text, value + "\n")
        check_rate_interfaces(fds, args, 100)
    passed("sysfs: sample rate 1–100000 Hz, invalid input rejected")


def test_ioctl(fds, args):
    for rate in (1, 100000, 100):
        set_rate(fds[0], rate)
        check_rate_interfaces(fds, args, rate)
    for rate in (0, 100001, 0xffffffff):
        expect_error(f"SET({rate})", errno.EINVAL, set_rate, fds[0], rate)
        check_rate_interfaces(fds, args, 100)
    expect_error("Unknown ioctl", errno.ENOTTY,
                 fcntl.ioctl, fds[0], (ord("A") << 8) | 255)
    for command in (GET_RATE, SET_RATE):
        expect_error("ioctl(NULL)", errno.EFAULT, fcntl.ioctl, fds[0], command, 0)
    passed("ioctl: GET/SET, EINVAL/ENOTTY/EFAULT; consistent with sysfs and /proc")


def test_read_and_stats(fds, args):
    before = check_proc(args.proc, 2, 100)
    for index, fd in enumerate(fds):
        check(f"CLEAR for client {index}", fcntl.ioctl(fd, CLEAR_BUFFER), 0)
        data = read_samples(fd)
        check(f"read for client {index}: bytes", len(data), 32)
    passed("Data: CLEAR call and 32-byte reads from both clients")

    after = check_proc(args.proc, 2, 100)
    check("/proc: samples read",
          sorted(row["samples_read"] for row in after["rows"]), [16, 16])
    check("/proc: total counter did not decrease",
          after["samples_generated"] >= before["samples_generated"], True)
    passed("/proc: 2 clients, valid buffer occupancy, 16 samples read each")


def run(args):
    check_proc(args.proc, 0)
    fds = []
    original_rate = None
    try:
        for index in range(2):
            fds.append(os.open(args.device, os.O_RDONLY))
            check_proc(args.proc, index + 1)
        passed("Open: client count 0 → 1 → 2")
        original_rate = get_rate(fds[0])
        test_sysfs(fds, args)
        test_ioctl(fds, args)
        test_read_and_stats(fds, args)
    finally:
        try:
            if original_rate is not None:
                set_rate(fds[0], original_rate)
                check("Restore sample rate", get_rate(fds[0]), original_rate)
        finally:
            for fd in fds:
                os.close(fd)
    check_proc(args.proc, 0)
    passed(f"Cleanup: sample rate restored ({original_rate} Hz), 0 clients")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="/dev/ad7683")
    parser.add_argument("--proc", default="/proc/ad7683")
    parser.add_argument("--sysfs", default="/sys/class/ad7683/ad7683")
    args = parser.parse_args()
    signal.signal(signal.SIGALRM, timeout_handler)
    print("Virtual AD7683 ADC test", flush=True)
    try:
        run(args)
    except (OSError, RuntimeError, ValueError, KeyError, KeyboardInterrupt) as exc:
        print(f"[FAIL] {exc or 'Test interrupted by user'}", file=sys.stderr)
        print("Result: test did not complete successfully.")
        return 1
    print("Result: all 6 test stages passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
