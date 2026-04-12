#ifndef KERNEL_SCHED_TASK_H
#define KERNEL_SCHED_TASK_H

#include <stdint.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 4096
#define TASK_NAME_LEN 16

typedef enum {
    UNUSED = 0,
    READY,
    RUNNING,
    BLOCKED,
    SLEEPING,
    DYING
} task_state_t;

typedef struct task {
    int id;
    task_state_t state;
    uint64_t *sp;
    void(*entry)(void);
    uint64_t wakeup_tick;
    char name[TASK_NAME_LEN];
    uint8_t stack[TASK_STACK_SIZE];
} task_t;

void task_init_system(void);
task_t *task_get(int id);

int task_create(void (*entry)(void), const char *name);
int task_request_stop(int id);
void task_reap_dying(int exclude_id);

#endif