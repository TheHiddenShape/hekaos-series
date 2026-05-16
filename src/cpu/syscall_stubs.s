.global trigger_syscall

# int32_t trigger_syscall(uint32_t num, arg0, arg1, arg2) — cdecl wrapper for int $0x80
trigger_syscall:
    push %ebx              # %ebx is callee-saved (SysV i386)
    mov 8(%esp), %eax
    mov 12(%esp), %ebx
    mov 16(%esp), %ecx
    mov 20(%esp), %edx
    int $0x80
    pop %ebx
    ret
