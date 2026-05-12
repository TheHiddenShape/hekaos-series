# HekaOS

![alt](assets/media/3dhekaos_banner.png)

An x86 monolithic hybrid kernel written from scratch in **C** and **Rust**.

This is a series leading to v1.0.0, the first stable release of a fully functional OS. Design decisions and architectural choices are documented on my personal [blog](https://ammons-organization-1.gitbook.io/thehiddenshape/system-and-networks/kfs-kernel-from-scratch-series).

## Internals

**hekashell**: interactive command-line interface with the following built-in commands:

preview**
![alt](https://i.imgur.com/GghDdPP.png)

| Command    | Description                                         |
|------------|-----------------------------------------------------|
| `help`     | show available commands                             |
| `dmesg`    | display the kernel ring buffer                      |
| `memdump`  | show memory usage (heap/vmalloc stats)              |
| `reboot`   | reboot the system                                   |
| `shutdown` | power off the system (ACPI)                         |
| `halt`     | halt the CPU                                        |
| `keymap`   | switch keyboard layout (`qwerty` \| `azerty`)       |
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
