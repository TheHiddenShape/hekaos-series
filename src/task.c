#include "klib.h"
#include "kmalloc.h"
#include "paging.h"
#include "sched.h"
#include <stddef.h>
#include <stdint.h>

#define KSTACK_PAGES 2

/* kernel stack VA base: one slot per process, just below the vmalloc zone */
#define KSTACK_VA_BASE 0xEF000000

uint32_t task_counter = 0;
struct task *task_list_head = NULL;
struct task *current_task = NULL;

struct task init_task = {
    .state = TASK_RUNNING,
    .pid = 0,
    .uid = 0,
    .euid = 0,
    .quantum = PROC_QUANTUM,
    .time_left = PROC_QUANTUM,
};

void
task_init (void)
{
    task_list_head = &init_task;
    current_task = &init_task;
    /* inherit the currently loaded kernel pgdir so the asm switch can safely
     * reload CR3 when other tasks switch back to init_task */
    init_task.mm.pgdir = read_cr3 ();
}

void
task_add_child (struct task *parent, struct task *child)
{
    child->parent = parent;
    child->sibling = parent->children;
    parent->children = child;
}

void
task_remove_child (struct task *parent, struct task *child)
{
    struct task *cur = parent->children;

    if (cur == child)
    {
        parent->children = child->sibling;
    }
    else
    {
        while (cur != NULL && cur->sibling != child)
        {
            cur = cur->sibling;
        }
        if (cur != NULL)
        {
            cur->sibling = child->sibling;
        }
    }

    child->sibling = NULL;
    child->parent = NULL;
}

void
exec_fn (uint32_t *addr, uint32_t *function, uint32_t size)
{
    if (size == 0 || task_counter >= MAX_PROC - 1)
    {
        return;
    }

    struct task *t = kmalloc (sizeof (struct task));
    if (!t)
    {
        return;
    }
    memset (t, 0, sizeof (struct task));
    t->state = TASK_BLOCKED;

    t->pid = ++task_counter;

    /* one slot of KSTACK_PAGES per pid in the dedicated VA zone */
    uint32_t kstack_va = KSTACK_VA_BASE + t->pid * KSTACK_PAGES * PAGE_SIZE;
    for (uint32_t p = 0; p < KSTACK_PAGES; p++)
    {
        void *va = (void *)(kstack_va + p * PAGE_SIZE);
        if (!alloc_page (va, PAGE_PRESENT | PAGE_RW))
        {
            for (uint32_t f = 0; f < p; f++)
            {
                free_page ((void *)(kstack_va + f * PAGE_SIZE));
            }
            kfree (t);
            --task_counter;
            return;
        }
    }
    uint32_t kstack_top = kstack_va + KSTACK_PAGES * PAGE_SIZE;

    kstack_top -= sizeof (struct trap_frame);
    struct trap_frame *tf = (struct trap_frame *)kstack_top;
    memset (tf, 0, sizeof (struct trap_frame));

    tf->eip = (uint32_t)addr;
    tf->cs = 0x08; /* kernel code segment (Ring 0); 0x1B when userland lands */
    tf->eflags = 0x202; /* IF=1, reserved bit set */
    tf->ds = 0x10;

    t->uid = current_task->uid;
    t->euid = current_task->euid;
    t->quantum = PROC_QUANTUM;
    t->time_left = PROC_QUANTUM;
    t->kstack = (void *)kstack_va;
    t->thread.tf = tf;
    t->thread.esp = kstack_top;
    t->mm.pgdir = paging_proc_init (t->pid);
    t->mm.code_start = (uint32_t)addr;
    t->mm.code_end = (uint32_t)addr + size;
    /* top of userland VA space; grows downward from KERNEL_VIRT_BASE */
    t->mm.stack_top = KERNEL_VIRT_BASE - PAGE_SIZE;
    t->parent = current_task;

    task_add_child (current_task, t);

    t->next = task_list_head;
    task_list_head = t;

    /* byte-granularity: x86 instructions are variable-length (1–15 bytes) */
    uint8_t *dst = (uint8_t *)addr;
    uint8_t *src = (uint8_t *)function;
    for (uint32_t i = 0; i < size; i++)
    {
        dst[i] = src[i];
    }

    t->state = TASK_RUNNABLE;
}

void
task_reparent (struct task *task, struct task *new_parent)
{
    if (task->parent != NULL)
    {
        task_remove_child (task->parent, task);
    }
    task_add_child (new_parent, task);
}
