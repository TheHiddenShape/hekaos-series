#ifndef TASK_H
#define TASK_H

#include "signal.h"
#include "trap_frame.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* per-task kernel stack size, in pages. Exposed so sched.c can compute
 * kstack_top = kstack + KSTACK_PAGES * PAGE_SIZE when updating TSS.esp0. */
#define KSTACK_PAGES 2

#define TASK_LOG_SIZE 1024

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

    uint32_t quantum;   /* reload value: ticks granted per scheduling round */
    uint32_t time_left; /* ticks remaining in the current quantum */

    int32_t exit_code; /* set on TASK_ZOMBIE, collected by parent via wait() */

    uint32_t uid;  /* real user id: owner of this process */
    uint32_t euid; /* effective user id: used for inter-process permission
                    * checks (signaling, ptrace); future: gid/egid when FS
                    * permissions land */

    sigset_t pending_signals;            /* bitmask of undelivered signals */
    sig_handler_t signal_handlers[NSIG]; /* per-signal handler */

    /* placed after every field consumed by irq_common_stub (thread.esp at +8,
     * mm.pgdir at +32). Adding fields here is safe; do not insert earlier. */
    bool is_userspace; /* true iff this task ever returns to Ring 3; gates
                        * the TSS.esp0 update in schedule() */

    char *log_buf;
    uint32_t log_head;
    uint32_t log_len;
};

#define MAX_PROC 128

extern uint32_t task_counter;
extern struct task *task_list_head;
extern struct task *current_task;
extern struct task *init_proc; /* PID 1: userland init / reaper */
extern struct task init_task;
extern struct task kthreadd_task; /* PID 2: sentinel parent of all kthreads */

void task_init (void);

/* boot-time structural check of task_init's init/kthreadd/pid invariants;
 * runtime scenarios are documented in task.c */
void task_test (void);

void exec_fn (uint32_t *addr, uint32_t *function, uint32_t size);

/* Spawn a Ring 3 process: maps a USER code page at USER_CODE_BASE filled with
 * `function`'s bytes, a USER stack just below KERNEL_VIRT_BASE, then crafts a
 * Ring0->Ring3 startup trap_frame on its kstack (cs=0x23, ds=0x2B,
 * user_ss=0x2B, user_esp=USER_STACK_TOP, eip=USER_CODE_BASE).
 *
 * Allocates the next PID from task_counter. PID 1 is owned by init (see
 * exec_init_fn) and never handed out here.
 *
 * `parent` becomes the new task's parent — must NOT be &kthreadd_task (that
 * sentinel is for kernel threads only). Pass init_proc for userland spawns. */
void exec_user_fn (uint32_t *function, uint32_t size, struct task *parent);

/* Spawn the Ring 3 init at the reserved PID 1, parented under init_task. Stores
 * and returns the new task in init_proc. Call once, at boot. */
struct task *exec_init_fn (uint32_t *function, uint32_t size);

/* duplicate current_task. Returns child pid (parent), 0 (child), -1 (fail).
 * frame is the syscall trap frame, cloned onto the child kstack with eax=0. */
int32_t task_fork (struct trap_frame *frame);

/* terminate t: reparent its children to init, mark ZOMBIE, notify parent via
 * SIGCHLD. shared by sys_exit and the signal default-terminate action. code is
 * the status later collected by wait(). */
void do_exit (struct task *t, int32_t code);

/* tear down a TASK_ZOMBIE: kstack, user pgdir, tree links, task struct.
 * CR3 must not be zombie's pgdir. */
void task_reap (struct task *zombie);

void task_log_putchar (struct task *t, char c);
void task_dump_log (uint32_t pid);

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
