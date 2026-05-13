#include "task.h"

uint32_t task_counter = 0;
struct task *task_list_head = NULL;

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
task_reparent (struct task *task, struct task *new_parent)
{
    if (task->parent != NULL)
    {
        task_remove_child (task->parent, task);
    }
    task_add_child (new_parent, task);
}
