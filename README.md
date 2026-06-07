# HekaOS

![alt](assets/media/white3dhekaos_banner.png)

An x86 monolithic hybrid kernel written from scratch in **C** and **Rust**.

This is a series leading to v1.0.0, the first stable release of a fully functional OS. Design decisions and architectural choices are documented on my personal [blog](https://ammons-organization-1.gitbook.io/thehiddenshape/system-and-networks/kfs-kernel-from-scratch-series).

## Internals

**hekashell**: interactive command-line interface with the following built-in commands:

preview**
![alt](https://i.imgur.com/GghDdPP.png)

| Command                  | Description                                      |
|--------------------------|--------------------------------------------------|
| `help`                   | show this help message                           |
| `man <cmd>`              | per-command manual page (basic, in v0.5.0)       |
| `def <cmd>`              | explain a command in detail                      |
| `reboot`                 | reboot the system                                |
| `shutdown`               | power off the system (ACPI)                      |
| `halt`                   | halt the CPU                                     |
| `keymap qwerty\|azerty`  | select keyboard layout                           |
| `traptest`               | trigger INT 0x42 and dump the trap frame         |
| `clear`                  | clear the terminal screen                        |
| `momentum`               | dump current task execution context              |
| `memdump`                | display memory usage summary                     |
| `dmesg`                  | display kernel ring buffer                       |
| `plog <pid>`             | dump a process output log                        |
| `pfeed <pid> <text>`     | inject input into a process                      |
| `eyeproc`                | full-screen process grid (ESC to quit)           |
| `spawntsk -k\|-u [N]`    | run kthread/user test N (or all)                 |
| `kill <pid> <sig>`       | send signal sig to pid                           |
| `signal <pid> <sig>`     | install a debug handler on pid for sig           |
## Building & Running

### Build with Docker (recommended)

```bash
make docker-build   # builds cross-compiler + kernel inside docker
make run-iso        # boot the ISO in QEMU
```

### Build with a local cross-compiler

```bash
make all            # produces kernel/hekaos.bin and kernel/hekaos.iso
make run-iso        # boot the ISO in QEMU
# or
make run-bin        # boot the raw binary in QEMU (no GRUB)
```

## Preview Releases

- v0.1.0 Primitives boot sequences
- v0.2.0 Memory
- v0.3.0 Interrupts
- v0.4.0 Processes
- v0.5.0 Filesystem
- v0.6.0 Syscalls, sockets & environment
- v0.7.0 Modules
- v0.8.0 ELF
- v0.9.0 Stabilization
- v1.0.0 First stable release

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
