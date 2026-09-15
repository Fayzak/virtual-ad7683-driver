#!/usr/bin/env python3
"""Проверка доставки отсчётов двум независимым клиентам /dev/ad7683."""

import argparse
import array
import errno
import fcntl
import os
import struct
import sys

GET_RATE = (2 << 30) | (4 << 16) | (ord("A") << 8)
SET_RATE = (1 << 30) | (4 << 16) | (ord("A") << 8) | 1
CLEAR_BUFFER = (ord("A") << 8) | 2

def get_rate(fd):
    value = bytearray(4)
    fcntl.ioctl(fd, GET_RATE, value)
    return struct.unpack("=I", value)[0]


def set_rate(fd, rate):
    fcntl.ioctl(fd, SET_RATE, struct.pack("=I", rate))

def expect_errno(expected, operation):
    try:
        operation()
    except OSError as exc:
        if exc.errno == expected:
            return
        raise
    raise RuntimeError(f"Ожидалась ошибка errno={expected}")

def test_ioctl(fds):
    original = get_rate(fds[0])
    try:
        for rate in (1, 100000, 100):
            set_rate(fds[0], rate)
            if any(get_rate(fd) != rate for fd in fds):
                raise RuntimeError("Частота должна быть общей для обоих клиентов")

        for rate in (0, 100001, 0xffffffff):
            expect_errno(errno.EINVAL, lambda: set_rate(fds[0], rate))
            if get_rate(fds[0]) != 100:
                raise RuntimeError("Неверный SET изменил частоту")

        expect_errno(errno.ENOTTY,
                     lambda: fcntl.ioctl(fds[0], (ord("A") << 8) | 255))
        expect_errno(errno.EFAULT, lambda: fcntl.ioctl(fds[0], GET_RATE, 0))
        expect_errno(errno.EFAULT, lambda: fcntl.ioctl(fds[0], SET_RATE, 0))

        # CLEAR may race with new samples, so an empty read is not guaranteed.
        for index, fd in enumerate(fds):
            fcntl.ioctl(fd, CLEAR_BUFFER)
            read_samples(fd, index)
        print("OK: GET/SET, диапазон частоты, ошибки ioctl, чтение после CLEAR.")
    finally:
        set_rate(fds[0], original)
        print(f"Восстановлена частота: {original} Гц")


def read_samples(fd, client_index):
    data = os.read(fd, 32)
    if not data:
        raise RuntimeError(f"Клиент {client_index}: неожиданный EOF")
    if len(data) % 2:
        raise RuntimeError("Получено нечётное количество байтов")
    if len(data) != 32:
        raise RuntimeError(f"Клиент {client_index}: ожидалось 32 байта")

    samples = array.array("H")
    samples.frombytes(data)
    print(f"Клиент {client_index}: {len(data)} байт, отсчёты: {list(samples)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="/dev/ad7683", help="Путь к устройству")
    args = parser.parse_args()

    fds = []
    try:
        for _ in range(2):
            fds.append(os.open(args.device, os.O_RDONLY))
        test_ioctl(fds)
    finally:
        for fd in fds:
            os.close(fd)

    print("OK: оба клиента получили отсчёты, дескрипторы закрыты.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as exc:
        print(f"Ошибка: {exc}", file=sys.stderr)
        sys.exit(1)
