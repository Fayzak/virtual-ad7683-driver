#!/usr/bin/env python3
"""Проверка доставки отсчётов двум независимым клиентам /dev/ad7683."""

import argparse
import array
import os
import sys


def read_samples(fd, client_index):
    data = os.read(fd, 32)
    if not data:
        raise RuntimeError(f"Клиент {client_index}: неожиданный EOF")
    if len(data) % 2:
        raise RuntimeError("Получено нечётное количество байтов")

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
        for index, fd in enumerate(fds):
            read_samples(fd, index)
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
