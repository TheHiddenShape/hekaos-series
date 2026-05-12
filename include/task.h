#ifndef TASK_H
#define TASK_H

#include "signal.h"
#include "trap_frame.h"
#include <stddef.h>
#include <stdint.h>

enum task_state
{
    TASK_RUNNABLE = 0, /* ready to be scheduled */
    TASK_RUNNING,      /* currently executing on the CPU */
    TASK_BLOCKED,      /* waiting on an event (I/O, sleep, lock) */
    TASK_ZOMBIE, /* terminated, waiting for parent to collect exit status */
};

/*
 * Architecture-specific CPU state for a process.
 * Each process has exactly one execution context : isolated here so arch
 * extensions (FPU, segments) can be added without touching struct task.
 */
struct task_thread
{
    struct trap_frame
        *tf;      /* register snapshot saved at interrupt or context switch */
    uint32_t esp; /* kernel stack pointer at the moment of the switch */
};

/*
 * Userland virtual address space descriptor.
 * Embedded by value in task; becomes a pointer with refcount when threads land
 * (clone(CLONE_VM) shares the same mm between tasks).
 *
 * Planned: struct vma *mmap: VMA list (code, data, BSS, heap, stack, mmaps)
 */
struct mm_struct
{
    uint32_t
        pgdir; /* physical address of the page directory, loaded into CR3 */

    uint32_t code_start, code_end; /* bounds of the text segment */
    uint32_t data_start, data_end; /* bounds of the data/BSS segment */
    uint32_t stack_top; /* top of the userland stack (grows downward) */

    void *heap_brk; /* current heap break: top of the heap segment (sbrk
                       target) */
};

/*
 * task descriptor: represents a process.
 *
 * Planned extensions: exit_code collected by the parent on TASK_ZOMBIE.
 */
struct task
{
    enum task_state state; /* current lifecycle state of this task */
    struct task_thread
        thread; /* arch CPU state, saved and restored on switch */

    uint32_t pid;        /* unique process identifier */
    struct task *parent; /* direct pointer to parent task (NULL for init) */

    /* n-ary process tree: children is the first child, sibling chains the rest
     */
    struct task *children;
    struct task *sibling;

    void *kstack;        /* base address of this task's kernel stack */
    struct mm_struct mm; /* userland virtual address space descriptor */

    struct task *next; /* intrusive singly-linked list for the scheduler,
                        * will become a list_head when needed */

    uint32_t uid;  /* real user id: owner of this process */
    uint32_t euid; /* effective user id: used for inter-process permission
                    * checks (signaling, ptrace); future: gid/egid when FS
                    * permissions land */

    sigset_t pending_signals;            /* bitmask of undelivered signals */
    sig_handler_t signal_handlers[NSIG]; /* per-signal handler */
};

#define MAX_PROC 128

extern uint32_t task_counter;

/* n-ary tree helpers */
void task_add_child (struct task *parent, struct task *child);
void task_remove_child (struct task *parent, struct task *child);
void task_reparent (struct task *task, struct task *new_parent);

/* iterate over direct children of parent and child (struct task *) is the loop
 * cursor */
#define task_for_each_child(parent, child)                                     \
    for ((child) = (parent)->children; (child) != NULL;                        \
         (child) = (child)->sibling)

#endif /* TASK_H */
