#ifndef KERNEL_DEBUG_DIAG_H
#define KERNEL_DEBUG_DIAG_H

#include <stdint.h>
#include "kernel/sched/task.h"
#include "kernel/memory/heap.h"

typedef struct
{
    int id;
    task_state_t state;
    uint32_t flags;
    uint64_t runtime_ticks;
    uint64_t runtime_delta_ticks;
    uint64_t switch_count;
    unsigned int stack_used_bytes;
    unsigned int stack_free_bytes;
    const char *name;
} diag_task_info_t;

typedef struct
{
    uint64_t tick;
    uint64_t total_runtime_delta_ticks;
    uint64_t idle_runtime_delta_ticks;
    unsigned int cpu_busy_percent;

    heap_stats_t heap;

    unsigned int task_count;
    diag_task_info_t tasks[MAX_TASKS];
} diag_snapshot_t;

void diag_init(void);
void diag_get_snapshot(diag_snapshot_t *snapshot);

#endif