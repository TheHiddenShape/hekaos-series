#ifndef SCHED_H
#define SCHED_H

#include "task.h"

#define PROC_QUANTUM 5 // 5ms

struct task *pick_next_task (void);

void schedule (void);
void round_robin_coordinator (void);

#endif
