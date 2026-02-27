.global outb
.global outw
.global inb
.global io_wait

outb:
    mov 8(%esp), %al
    mov 4(%esp), %dx
    out %al, %dx
    ret

inb:
    mov 4(%esp), %dx
    in %dx, %al
    ret

outw:
    mov 8(%esp), %ax
    mov 4(%esp), %dx
    out %ax, %dx
    ret

io_wait:
    xor %eax, %eax
    mov $0x80, %dx
    out %al, %dx
    ret
