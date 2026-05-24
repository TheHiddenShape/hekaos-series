.global ufork_fn
.global ufork_fn_end
.global uid_fn
.global uid_fn_end
.global uspin_fn
.global uspin_fn_end

# ufork: fork; child exit(1); parent waitpid then exit(0). Silent.
ufork_fn:
    mov $2, %eax # SYS_FORK
    xor %ebx, %ebx
    xor %ecx, %ecx
    xor %edx, %edx
    int $0x80
    test %eax, %eax
    jnz .Lparent # eax > 0 in parent (child pid); 0 in child

.Lchild:
    mov $1, %eax # SYS_EXIT
    mov $1, %ebx # status 1
    int $0x80
.Lchild_hang:
    jmp .Lchild_hang

.Lparent:
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

# uid: SYS_GETUID, discard the return value, exit(0). Silent.
uid_fn:
    mov $199, %eax # SYS_GETUID
    int $0x80
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
