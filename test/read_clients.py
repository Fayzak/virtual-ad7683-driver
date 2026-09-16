#!/usr/bin/env python3
"""Проверки ioctl, чтения и /proc/ad7683. Запускать без других клиентов драйвера."""

import argparse
import array
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


class Report:
    def __init__(self):
        self.passed = 0
        self.failed = 0

    def check(self, name, actual, expected):
        ok = actual == expected
        self.passed += int(ok)
        self.failed += int(not ok)
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: "
              f"ожидалось {expected}, получено {actual}", flush=True)
        if not ok:
            raise RuntimeError(name)

    def error(self, name, expected, operation):
        actual = 0
        try:
            operation()
        except OSError as exc:
            actual = exc.errno
        self.check(name, actual, expected)


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
        raise RuntimeError("Неполный заголовок /proc")
    for row in result["rows"]:
        if set(row) != {"capacity", "buffered", "samples_read", "overruns"}:
            raise RuntimeError("Неполная статистика клиента")
    return result


def check_proc(report, path, expected_clients, rate=None):
    stats = read_proc(path)
    report.check("/proc: количество клиентов", stats["clients"], expected_clients)
    report.check("/proc: число строк клиентов", len(stats["rows"]), expected_clients)
    if rate is not None:
        report.check("/proc: частота", stats["sample_rate_hz"], rate)
    for index, row in enumerate(stats["rows"]):
        valid = (row["capacity"] > 0 and 0 <= row["buffered"] <= row["capacity"]
                 and row["samples_read"] >= 0 and row["overruns"] >= 0)
        report.check(f"/proc: границы счётчиков клиента {index}", valid, True)
    return stats


def timeout_handler(signum, frame):
    raise TimeoutError("Чтение не завершилось за 5 секунд")


def read_samples(fd):
    signal.alarm(5)
    try:
        return os.read(fd, 32)
    finally:
        signal.alarm(0)


def run(report, args):
    check_proc(report, args.proc, 0)
    fds = []
    original = None
    try:
        for index in range(2):
            fds.append(os.open(args.device, os.O_RDONLY))
            check_proc(report, args.proc, index + 1)
        original = get_rate(fds[0])
        for rate in (1, 100000, 100):
            set_rate(fds[0], rate)
            for index, fd in enumerate(fds):
                report.check(f"SET/GET: клиент {index}", get_rate(fd), rate)
            check_proc(report, args.proc, 2, rate)

        for rate in (0, 100001, 0xffffffff):
            report.error(f"SET({rate}): EINVAL", errno.EINVAL,
                         lambda: set_rate(fds[0], rate))
            report.check("Частота после отклонённого SET", get_rate(fds[0]), 100)
        report.error("Неизвестная команда: ENOTTY", errno.ENOTTY,
                     lambda: fcntl.ioctl(fds[0], (ord("A") << 8) | 255))
        for cmd, name in ((GET_RATE, "GET"), (SET_RATE, "SET")):
            report.error(f"{name}(NULL): EFAULT", errno.EFAULT,
                         lambda: fcntl.ioctl(fds[0], cmd, 0))

        before = check_proc(report, args.proc, 2, 100)
        for index, fd in enumerate(fds):
            report.check(f"CLEAR клиента {index}: код возврата",
                         fcntl.ioctl(fd, CLEAR_BUFFER), 0)
            data = read_samples(fd)
            report.check(f"read клиента {index}: байты", len(data), 32)
            samples = array.array("H")
            samples.frombytes(data)
            print(f"  Отсчёты: {list(samples)}", flush=True)
        after = check_proc(report, args.proc, 2, 100)
        report.check("/proc: каждый клиент извлёк 16 отсчётов",
                     sorted(row["samples_read"] for row in after["rows"]), [16, 16])
        report.check("/proc: общий счётчик не уменьшился",
                     after["samples_generated"] >= before["samples_generated"], True)
        print("CLEAR: проверены вызов и чтение после него; новые отсчёты могут "
              "появиться сразу после очистки.", flush=True)
    finally:
        try:
            if original is not None:
                set_rate(fds[0], original)
                report.check("Восстановление частоты", get_rate(fds[0]), original)
        finally:
            for fd in fds:
                os.close(fd)
    check_proc(report, args.proc, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="/dev/ad7683")
    parser.add_argument("--proc", default="/proc/ad7683")
    args = parser.parse_args()
    report = Report()
    signal.signal(signal.SIGALRM, timeout_handler)
    try:
        run(report, args)
    except (OSError, RuntimeError, ValueError, KeyError, KeyboardInterrupt) as exc:
        if report.failed == 0:
            report.failed += 1
        print(f"[FAIL] Тест остановлен: {exc}", file=sys.stderr)
    print(f"Итого: PASS={report.passed}, FAIL={report.failed}")
    return int(report.failed != 0)


if __name__ == "__main__":
    sys.exit(main())
