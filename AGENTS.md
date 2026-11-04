# AGENTS.md

## Project overview
- Currently, this is a virtual ADC character-device driver; it does not use SPI.
- SPI support is planned. Implement it only within an explicitly assigned task.
- Target kernel: Linux 6.12.107.

## Build and test commands
- Build: `make build` without sudo.
- For a custom kernel: `make build KERNEL_DIR=/path/to/kernel/build`.
- In the test VM: `sudo make run`, then `sudo python3 test/read_clients.py`.
- Close all device clients before `sudo make remove`.
- Use headers matching the target kernel; load the module only into that kernel.
- Ask for permission before each privileged command.
- Diagnose command failures before requesting elevated privileges.

## Code style guidelines
- Follow Linux kernel coding style and the repository's `.clang-format`.
- Keep formatting changes limited to the code being modified.

## Testing instructions
- After driver changes, build the module and run relevant tests in the test VM.
- Run `test/read_clients.py` after loading the module, with no unrelated clients.
- Check kernel logs for new WARN/Oops messages.
- Report each check in a table: command/case, PASS/FAIL/NOT RUN, and details.
- Never report an unexecuted check as passed; explain missing verification.

## Compatibility constraints
- Preserve `/dev/ad7683`, the native-endian unsigned 16-bit sample format, and existing `read()` behavior, including blocking/nonblocking semantics and error codes.
- Preserve ioctl command numbers, argument layouts, and semantics.
- Preserve existing sysfs paths and behavior, and procfs field names, formats, and meanings.
- Obtain user approval before introducing incompatible changes.
