.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    cli
    push $0
    push $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    cli
    push $\num
    jmp isr_common_stub
.endm

ISR_NOERRCODE  0
ISR_NOERRCODE  1
ISR_NOERRCODE  2
ISR_NOERRCODE  3
ISR_NOERRCODE  4
ISR_NOERRCODE  5
ISR_NOERRCODE  6
ISR_NOERRCODE  7
ISR_ERRCODE    8
ISR_NOERRCODE  9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE  66
ISR_NOERRCODE 128

.macro IRQ num, irq_num
.global irq\num
irq\num:
    cli
    push $0
    push $\irq_num
    jmp irq_common_stub
.endm

IRQ 0, 32
IRQ 1, 33

isr_common_stub:
    pusha

    mov %ds, %ax
    push %eax

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp
    call isr_handler
    add $4, %esp

    push %esp
    call signal_check_and_deliver
    add $4, %esp

    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa
    add $8, %esp
    iret

irq_common_stub:
    pusha

    mov %ds, %ax
    push %eax

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp
    call irq_handler
    add $4, %esp

    cmpl $0, need_resched
    je .Lno_resched

    # pick the current task and move the current stack pointer in task_thread->esp
    # saving routine
    mov current_task, %eax
    mov %esp, 8(%eax)

    # current_task = next_task; esp = next->thread.esp
    # switch stack pointer to next task
    mov next_task, %eax
    mov %eax, current_task
    mov 8(%eax), %esp

    # reload CR3 only if pgdir differs; mm.pgdir lives at +32
    # i.e. multiples threads of same proc
    mov 32(%eax), %ebx
    mov %cr3, %ecx
    cmp %ebx, %ecx
    je .Lskip_cr3
    mov %ebx, %cr3

.Lskip_cr3:
    movl $0, need_resched

.Lno_resched:
    push %esp
    call signal_check_and_deliver
    add $4, %esp

    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa
    add $8, %esp
    iret
