# Virtual AD7683 Driver

A Linux character-device driver that simulates a 16-bit ADC with a sawtooth signal: `0, 1, …, 65535, 0, …`.

Each separate `open()` creates a client with its own 10,000-sample ring buffer. Full buffers drop new samples. The default sample rate is 10,000 Hz; the configurable range is 1–100,000 Hz.

## Per-open buffering

A buffer belongs to an independent `open()`, not to a process ID. Two processes
that each open the device get separate buffers; reading one does not consume the
other's data. Two opens in one process also create two buffers. Descriptors shared
through `dup()` or inherited through `fork()` share the same buffer unless the
child opens the device separately.

## Build and load

Requires a C compiler, Make, and kernel headers matching the target kernel. Tested with Linux 6.12.

```bash
make build
sudo insmod ./virtual-ad7683.ko
ls -l /dev/ad7683
```

For a custom kernel:

```bash
make build KERNEL_DIR=/path/to/kernel/build
```

Close device clients before unloading:

```bash
sudo rmmod virtual_ad7683
make clean
```

## Read and write

`read()` returns native-endian unsigned 16-bit samples. Request an even number of bytes. Blocking reads wait for the full request; signals or errors may cause short reads. With `O_NONBLOCK`, reads return available data or `EAGAIN` when empty.

Read 16 samples:

```bash
sudo python3 - <<'PY'
import array
import os

fd = os.open('/dev/ad7683', os.O_RDONLY)
try:
    samples = array.array('H')
    samples.frombytes(os.read(fd, 32))
    print(list(samples))
finally:
    os.close(fd)
PY
```

`write()` is not supported. For example, writing with `echo 100 | sudo tee /dev/ad7683` fails. Use ioctl or sysfs to configure the device.

## ioctl

Commands are defined in `inc/device.h`:

| Command | Definition | Action |
| --- | --- | --- |
| `GET_SAMPLE_RATE` | `_IOR('A', 0, __u32)` | Read the global rate in Hz |
| `SET_SAMPLE_RATE` | `_IOW('A', 1, __u32)` | Set the global rate, 1–100,000 Hz |
| `CLEAR_BUFFER` | `_IO('A', 2)` | Clear only this client's buffer; keep statistics |

GET/SET take a pointer to a 32-bit unsigned value. Invalid rates return `EINVAL`, invalid pointers `EFAULT`, and unknown commands `ENOTTY`.

Python example for Linux x86/x86-64 (sets the rate to 100 Hz):

```bash
sudo python3 - <<'PY'
import fcntl
import os
import struct

GET_RATE = (2 << 30) | (4 << 16) | (ord('A') << 8)
SET_RATE = (1 << 30) | (4 << 16) | (ord('A') << 8) | 1
CLEAR_BUFFER = (ord('A') << 8) | 2

fd = os.open('/dev/ad7683', os.O_RDONLY)
try:
    fcntl.ioctl(fd, SET_RATE, struct.pack('=I', 100))
    value = bytearray(4)
    fcntl.ioctl(fd, GET_RATE, value)
    print('Sample rate:', struct.unpack('=I', value)[0], 'Hz')
    fcntl.ioctl(fd, CLEAR_BUFFER)
finally:
    os.close(fd)
PY
```

## /proc statistics

```bash
cat /proc/ad7683
```

A read-only snapshot contains:

- `sample_rate_hz`, `samples_generated`, `clients`;
- per client: `capacity`, `buffered`, `samples_read`, `overruns`.

Buffer sizes are in samples. `samples_read` counts samples removed from the buffer; `overruns` counts dropped new samples. Client numbers are snapshot row indices, not PIDs. Reopen the file to refresh the snapshot.

## /sys configuration

```bash
cat /sys/class/ad7683/ad7683/sample_rate
echo 1000 | sudo tee /sys/class/ad7683/ad7683/sample_rate
```

`sample_rate` accepts a decimal rate from 1 to 100,000 Hz. Changes affect all clients and use the same setter as ioctl. Existing buffers and the sawtooth sequence are preserved.

## Tests

The test script was developed collaboratively with chatgpt-6-astra.

Run after loading the module, with no other device clients active:

```bash
sudo python3 test/read_clients.py
```

The script checks client registration, sysfs, ioctl, reads after CLEAR, and proc statistics. It also starts two separate processes that each open the device and verifies independent read counters. It prints a compact PASS/FAIL report, restores the original rate, and closes its clients.
