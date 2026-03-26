# HekaOS v0.1.0

An x86 monolithic hybrid kernel written from scratch in **C** and **Rust**.

This is the first milestone in a series leading to v1.0.0, the first stable release of a fully functional OS. Design decisions and architectural choices are documented on my personal [blog](https://ammons-organization-1.gitbook.io/thehiddenshape/system-and-networks/kfs-kernel-from-scratch-series).

## Core Features

- **GDT (Global Descriptor Table)** - segmentation setup for protected mode (even though in protected mode it's more of a privilege table than anything else...)
- **IDT (Interrupt Descriptor Table)** - interrupt and exception handling
- **VGA text mode** - 80x25 screen output for display and shell rendering
- **Keyboard driver** - PS/2 keyboard input via IRQ1
- **Kernel stack** - properly aligned stack for kernel execution
- **Kernel ring buffer** - internal log buffer for kernel messages
- **Command-line interface** - interactive shell (`hekashell`) with the following built-in commands:

| Command    | Description                            |
|------------|----------------------------------------|
| `help`     | Show available commands                |
| `dmesg`    | Display the kernel ring buffer         |
| `reboot`   | Reboot the system                      |
| `shutdown` | Power off the system (ACPI)            |
| `halt`     | Halt the CPU                           |

## Building & Running

### Build with Docker (recommended)

```bash
make docker-build   # Builds cross-compiler + kernel inside Docker
make run-iso        # Boot the ISO in QEMU
```

### Build with a local cross-compiler

```bash
make all            # Produces kernel/hekaos.bin and kernel/hekaos.iso
make run-iso        # Boot the ISO in QEMU
# or
make run-bin        # Boot the raw binary in QEMU (no GRUB)
```

## Roadmap

### v0.1.0 - Boot Sequence Primitives ✅

A bootable kernel loaded by GRUB, built on an assembly entry point, with a minimal library providing basic types and utility functions. Writes to VGA for screen output. Configures and loads the GDT and IDT to handle keyboard interrupts.

### v0.2.0 - Memory ✅

A complete memory management system with pagination handling. R/W rights on memory. User space and kernel space separation. Physical and virtual memory management. Kernel heap allocator (kmalloc, kfree, ksize, kbrk): returns virtual addresses backed by physically contiguous pages, operating on top of the paging layer. Virtual memory allocator (vmalloc, vfree, vsize, vbrk): provides large, virtually contiguous allocations mapped across physically fragmented pages through page tables, suitable for large buffers where physical contiguity unavailable. Kernel panic handling.

### v0.3.0 - Interrupts

Hardware and software interrupts. A full interrupt descriptor table. Signal handling and scheduling. Global panic fault handling. Register cleaning and stack saving.

### v0.4.0 - Processes

Basic data structures for processes. Process interconnection (kinship, signals, sockets). Process ownership and rights. Helpers for syscalls: `fork`, `wait`, `_exit`, `getuid`, `signal`, `kill`. Process interruptions, memory separation, and multitasking.

### v0.5.0 - Filesystem

A complete interface to read/write an IDE. A complete interface to read/write/delete an ext2 filesystem. A basic file tree (`/sys`, `/var`, `/dev`, `/proc`, `/sys`).

### v0.6.0 - Syscalls, Sockets & Environment

A complete syscall table and syscall system. A complete Unix environment. User accounts with login and password. Password protection. Inter-process communication sockets. A Unix-like filesystem hierarchy.

### v0.7.0 - Modules

Registering kernel modules (creation/destruction). Loading modules at boot time. Communication and callback interfaces between the kernel and modules.

### v0.8.0 - ELF

A complete interface to read, parse, store, and execute ELF files. Syscalls to read ELF files and launch processes from them. A kernel module in ELF format, ready to be inserted at runtime.

### v1.0.0 - First Stable Release

Fully functional basic binaries (`/bin/*`). A libc implementation. A POSIX-compliant shell.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
