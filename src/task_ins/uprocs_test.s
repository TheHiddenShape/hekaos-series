# Ring 3 demo workloads.
#
# Why assembly and not C: exec copies the raw bytes of a SINGLE function to a
# user page and jumps to it, so everything the code needs at runtime must live
# inside those bytes. Anything pointing OUTSIDE the function does not survive the
# copy: a `call` to a helper (the relative displacement breaks once the code is
# moved, and the kernel target is not USER-mapped anyway) and any .rodata or
# global reached by absolute address (it still points into kernel memory, a
# supervisor page -> #PF in Ring 3). C emits such external references on its own
# (string literals, switch jump tables, implicit memset/memcpy, stack canaries)
# with no way to guarantee it won't, so it cannot be made reliably self-contained
# here. Assembly keeps the payload position-independent BY CONSTRUCTION: only
# immediates, the user stack, int $0x80 inline, and internal relative jumps
# (which DO survive the copy). Once a real loader exists (separately-linked
# program / ELF), C user code becomes viable again (draft section 11, c/d).
#
# Linux i386 syscall ABI used by syscall_dispatch: eax = number, ebx/ecx/edx =
# args, int $0x80, return value in eax.
#   SYS_EXIT=1  SYS_FORK=2  SYS_WRITE=4  SYS_WAITPID=7  SYS_GETUID=199
#
# fn / fn_end sentinels: the exec-user path copies (fn_end - fn) bytes.

.global uhello_fn
.global uhello_fn_end
.global ufork_fn
.global ufork_fn_end
.global uid_fn
.global uid_fn_end
.global uspin_fn
.global uspin_fn_end

# uhello: write "ring3\n" to stdout, then exit(0)
uhello_fn:
    sub $8, %esp # scratch buffer on the user stack
    movb $0x72, 0(%esp) # 'r'
    movb $0x69, 1(%esp) # 'i'
    movb $0x6e, 2(%esp) # 'n'
    movb $0x67, 3(%esp) # 'g'
    movb $0x33, 4(%esp) # '3'
    movb $0x0a, 5(%esp) # '\n'

    mov $4, %eax # SYS_WRITE
    mov $1, %ebx # fd = stdout
    mov %esp, %ecx # buf -> stack scratch (user VA, mapped USER)
    mov $6, %edx # len = 6
    int $0x80
    add $8, %esp

    mov $1, %eax # SYS_EXIT
    xor %ebx, %ebx # status 0
    int $0x80
.Lhello_hang:
    jmp .Lhello_hang # safety: exit must not return
uhello_fn_end:

# ufork: fork; child writes "C\n" exit(1); parent writes "P\n" waitpid exit(0)
ufork_fn:
    mov $2, %eax # SYS_FORK
    xor %ebx, %ebx
    xor %ecx, %ecx
    xor %edx, %edx
    int $0x80
    test %eax, %eax
    jnz .Lparent # eax > 0 in parent (child pid); 0 in child

.Lchild:
    sub $4, %esp
    movb $0x43, 0(%esp) # 'C'
    movb $0x0a, 1(%esp) # '\n'
    mov $4, %eax # SYS_WRITE
    mov $1, %ebx
    mov %esp, %ecx
    mov $2, %edx
    int $0x80
    add $4, %esp
    mov $1, %eax # SYS_EXIT
    mov $1, %ebx # status 1
    int $0x80
.Lchild_hang:
    jmp .Lchild_hang

.Lparent:
    sub $4, %esp
    movb $0x50, 0(%esp) # 'P'
    movb $0x0a, 1(%esp) # '\n'
    mov $4, %eax # SYS_WRITE
    mov $1, %ebx
    mov %esp, %ecx
    mov $2, %edx
    int $0x80
    add $4, %esp
    mov $7, %eax # SYS_WAITPID
    xor %ebx, %ebx # wstatus = NULL
    xor %ecx, %ecx
    xor %edx, %edx
    int $0x80
    mov $1, %eax # SYS_EXIT
    xor %ebx, %ebx # status 0
    int $0x80
.Lparent_hang:
    jmp .Lparent_hang
ufork_fn_end:

# uid: getuid, print the digit + newline, exit(0). Assumes uid 0..9 (demo).
uid_fn:
    mov $199, %eax # SYS_GETUID
    int $0x80
    add $0x30, %al # uid -> ASCII digit
    sub $4, %esp
    mov %al, 0(%esp) # store digit first (before clobbering eax)
    movb $0x0a, 1(%esp) # '\n'
    mov $4, %eax # SYS_WRITE
    mov $1, %ebx
    mov %esp, %ecx
    mov $2, %edx
    int $0x80
    add $4, %esp
    mov $1, %eax # SYS_EXIT
    xor %ebx, %ebx
    int $0x80
.Luid_hang:
    jmp .Luid_hang
uid_fn_end:

# uspin: pure compute loop, never exits. No syscall, no data. Visible as
# RUNNING in eyeproc -> proves a Ring 3 task is being scheduled.
uspin_fn:
    xor %eax, %eax
.Lspin:
    inc %eax
    jmp .Lspin
uspin_fn_end:
