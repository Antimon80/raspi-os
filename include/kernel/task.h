#ifndef KERNEL_TASK_H
#define KERNEL_TASK_H

#include <stdint.h>

#define MAX_TASKS 8
#define TASK_STACK_SIZE 4096

typedef enum {
    UNUSED = 0,
    READY,
    RUNNING,
    BLOCKED,
    SLEEPING
} task_state_t;

typedef struct task {
    int id;
    task_state_t state;
    uint64_t *sp;
    void(*entry)(void);
    uint64_t wakeup_tick;
    uint8_t stack[TASK_STACK_SIZE];
} task_t;

void task_init_system(void);
int task_create(void (*entry)(void));
task_t *task_get(int id);

#endif