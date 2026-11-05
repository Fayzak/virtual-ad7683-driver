#!/usr/bin/env python3
"""Test interfaces and sawtooth samples. Run with no other driver clients."""

# This test script was developed collaboratively with chatgpt-6-astra.

import argparse
import errno
import fcntl
import os
import multiprocessing
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
    raise TimeoutError("Timed out waiting for driver data")


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


def read_sawtooth(fd, total_samples=65538):
    """Check native-endian u16 samples, preserving continuity across reads."""
    previous = None
    samples_read = 0
    wraps = 0
    mismatch = None

    # One deadline for the entire stream, including short/interrupted reads.
    signal.alarm(30)
    try:
        while samples_read < total_samples:
            count = min(512, total_samples - samples_read) * 2
            try:
                data = os.read(fd, count)
            except InterruptedError:
                continue
            if not data:
                raise RuntimeError(f"Sawtooth: unexpected EOF after {samples_read} samples")
            if len(data) % 2 or len(data) > count:
                raise RuntimeError(f"Sawtooth: invalid read length {len(data)}")

            for (value,) in struct.iter_unpack("=H", data):
                if previous is not None:
                    expected = (previous + 1) & 0xffff
                    if value != expected and mismatch is None:
                        mismatch = (f"sample {samples_read}: expected {expected}, "
                                    f"got {value} (previous {previous})")
                    if previous == 65535 and value == 0:
                        wraps += 1
                previous = value
                samples_read += 1
    except TimeoutError as exc:
        raise TimeoutError(
            f"Sawtooth: 30-second deadline exceeded after {samples_read} samples"
        ) from exc
    finally:
        signal.alarm(0)

    return mismatch, wraps


def test_sawtooth(args):
    check_proc(args.proc, 0)
    fd = os.open(args.device, os.O_RDONLY)
    original_rate = None
    try:
        original_rate = get_rate(fd)
        set_rate(fd, 10000)
        check("Sawtooth: CLEAR", fcntl.ioctl(fd, CLEAR_BUFFER), 0)
        before = check_proc(args.proc, 1, 10000)["rows"][0]

        # The generator runs before open(); CLEAR does not reset its phase.
        total_samples = 65538
        mismatch, wraps = read_sawtooth(fd, total_samples)
        after = check_proc(args.proc, 1, 10000)["rows"][0]
        overruns = after["overruns"] - before["overruns"]
        if overruns != 0:
            raise RuntimeError(
                f"Sawtooth: overruns delta={overruns}; cannot establish a loss-free "
                f"stream; first discontinuity: {mismatch or 'none observed'}"
            )
        if mismatch is not None:
            raise RuntimeError(f"Sawtooth: {mismatch}; overruns delta=0")
        check("Sawtooth: samples_read delta",
              after["samples_read"] - before["samples_read"], total_samples)
        check("Sawtooth: observed 65535 -> 0", wraps >= 1, True)
    finally:
        try:
            if original_rate is not None:
                set_rate(fd, original_rate)
                check("Sawtooth: restore sample rate", get_rate(fd), original_rate)
        finally:
            os.close(fd)
    check_proc(args.proc, 0, original_rate)
    passed("Sawtooth: 65538 native-endian u16 samples, step +1, wrap, no overruns; "
           "rate restored and client closed")


def process_client(device, connection):
    fd = None
    try:
        signal.signal(signal.SIGALRM, timeout_handler)
        fd = os.open(device, os.O_RDONLY)
        connection.send(("ready", os.getpid()))
        while True:
            command = connection.recv()
            if command == "stop":
                break
            if command != "read":
                raise RuntimeError(f"Unknown worker command: {command}")
            data = read_samples(fd)
            connection.send(("data", len(data)))
    except (OSError, RuntimeError, EOFError) as exc:
        connection.send(("error", str(exc)))
    finally:
        if fd is not None:
            os.close(fd)
        connection.close()


def receive_worker(connection, expected):
    if not connection.poll(10):
        raise TimeoutError("Client process did not respond within 10 seconds")
    kind, value = connection.recv()
    if kind != expected:
        raise RuntimeError(f"Client process: expected {expected}, got {kind}: {value}")
    return value


def test_process_clients(args):
    context = multiprocessing.get_context("spawn")
    workers = []
    connections = []
    rate_path = Path(args.sysfs) / "sample_rate"
    original_rate = int(rate_path.read_text())
    check_proc(args.proc, 0)
    try:
        rate_path.write_text("100\n")
        for _ in range(2):
            parent, child = context.Pipe()
            worker = context.Process(target=process_client, args=(args.device, child))
            try:
                worker.start()
            except BaseException:
                parent.close()
                child.close()
                raise
            child.close()
            workers.append(worker)
            connections.append(parent)

        pids = [receive_worker(pipe, "ready") for pipe in connections]
        check("Two separate client processes", len(set(pids)), 2)
        stats = check_proc(args.proc, 2, 100)
        check("Initial process counters",
              sorted(row["samples_read"] for row in stats["rows"]), [0, 0])

        for index, pipe in enumerate(connections):
            pipe.send("read")
            check(f"Process {index}: read bytes", receive_worker(pipe, "data"), 32)
            stats = check_proc(args.proc, 2, 100)
            expected = [0, 16] if index == 0 else [16, 16]
            check("Independent process read counters",
                  sorted(row["samples_read"] for row in stats["rows"]), expected)

        for pipe in connections:
            pipe.send("stop")
        for worker in workers:
            worker.join(5)
            check("Client process exit", worker.exitcode, 0)
    finally:
        for worker in workers:
            if worker.is_alive():
                worker.terminate()
            worker.join(2)
            if worker.is_alive():
                worker.kill()
                worker.join()
        for pipe in connections:
            pipe.close()
        rate_path.write_text(f"{original_rate}\n")
    check_proc(args.proc, 0, original_rate)
    passed("Per-open: two processes, separate buffers and independent read counters")


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
        test_process_clients(args)
        test_sawtooth(args)
    except (OSError, RuntimeError, ValueError, KeyError, EOFError, KeyboardInterrupt) as exc:
        print(f"[FAIL] {exc or 'Test interrupted by user'}", file=sys.stderr)
        print("Result: test did not complete successfully.")
        return 1
    print("Result: all 8 test stages passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
