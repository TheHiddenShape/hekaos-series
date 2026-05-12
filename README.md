# HekaOS

![alt](assets/media/3dhekaos_banner.png)

An x86 monolithic hybrid kernel written from scratch in **C** and **Rust**.

This is a series leading to v1.0.0, the first stable release of a fully functional OS. Design decisions and architectural choices are documented on my personal [blog](https://ammons-organization-1.gitbook.io/thehiddenshape/system-and-networks/kfs-kernel-from-scratch-series).

## Internals

**CPU & Interrupts**: 7-entry GDT with ring 0/3 segmentation, 256-entry IDT covering CPU exceptions (0x00–0x13) and hardware IRQs remapped to INT 32–47 via the 8259 PIC. Each exception handler captures a full trap frame with CR2 reporting on page faults. Unrecoverable faults trigger `kpanic`, which halts the kernel with a full register dump and trap frame display.


**Memory**: bitmap PMM over 64 MiB of physical RAM (4 KiB frames from 4 MiB), with `phys_alloc_contiguous(n)` for DMA requirements. 32-bit paging with identity-mapped first 4 MiB, recursive page directory at PD[1023], and a 3GB/1GB kernel/user split. Three allocators cover the virtual address space: `kmem_dyn_alloc` (`0xC0000000`) is a general-purpose heap with first-fit strategy, 8-byte alignment, and bi-directional coalescing; `kmalloc` (`0xD0000000`) is a slab-style pool with 6 fixed-size caches (8–256 bytes) and O(1) alloc/free; `vmalloc` (`0xF0000000`) handles large virtually contiguous regions backed by physically non-contiguous frames.


**Drivers**: VGA 80x25 text mode with hardware-panned scrolling (VGA start address register, no memmove on newline), keyboard-driven scrollback through history via arrow and page keys, cursor tracking, and a themed status bar. PS/2 keyboard input via IRQ1 with US QWERTY scancode mapping. Port I/O via `inb`/`outb`/`outw`/`io_wait` primitives.


**Signals**: Linux i386 ABI-compatible signal numbering (SIGHUP–SIGTSTP). CPU exceptions are mapped to their POSIX counterparts (`#PF` -> SIGSEGV, `#DE` -> SIGFPE, etc.) via `signal_from_exception()`. Supports handler registration, default actions (terminate/ignore), and a per-task pending signal bitmask.


**Utilities**: `printk` with 6 log levels (emerg -> debug) and a 4 KiB circular ring buffer. klib covers `memset`, `strlen`, `strcmp`. Runtime stack layout and usage reporting via kernel stack info.


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

### v0.1.0: Primitives boot sequences

a bootable kernel loaded by GRUB, built on an assembly entry point, with a minimal library providing basic types and utility functions. Writes to VGA for screen output. Configures and loads the GDT and IDT to handle keyboard interrupts.

### v0.2.0: Memory

the memory subsystem covers the following: pagination, read/write permissions, user/kernel space separation, physical/virtual memory management, and heap allocator helpers, alongside kernel panic handling.

### v0.3.0: Interrupts

hardware interrupts, software interrupts, an interrupts descriptor table, signal handling and scheduling, global panic fault handling, stack saving

### v0.4.0: Processes

basic data structures for processes. Process interconnection (kinship, signals, sockets). Process ownership and rights. Helpers for syscalls: `fork`, `wait`, `_exit`, `getuid`, `signal`, `kill`. Process interruptions, memory separation, and multitasking.

### v0.5.0: Filesystem

a complete interface to read/write an IDE, complete interface to read/write/delete an ext2 filesystem, basic file tree (`/sys`, `/var`, `/dev`, `/proc`, `/sys`).

### v0.6.0: Syscalls, sockets & environment

a complete syscall table and syscall system, complete Unix environment, user accounts with login and password, password protection, inter-process communication sockets, Unix-like filesystem hierarchy.

### v0.7.0: Modules

registering kernel modules (creation/destruction), loading modules at boot time, implementing functions for communication/callback between the kernel and the modules.

### v0.8.0: ELF

a complete interface to read, parse, store, and execute ELF files, syscalls to read ELF files and launch a process with them, a kernel module in ELF, ready to be inserted at run time.

### v0.9.0: Stabilization

kernel-wide hardening, bug fixing, and reliability improvements in preparation for the first stable release.

### v1.0.0: First stable release

fully functional basic binaries (`/bin/*`), libc implementation, POSIX-compliant shell.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
