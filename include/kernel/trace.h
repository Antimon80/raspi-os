#ifndef KERNEL_TRACE_H
#define KERNEL_TRACE_H

#include <stdint.h>

/*
 * Types of trace events recorded by the kernel.
 *
 * These events are used for debugging and diagnostics of task scheduling
 * and lifecycle transitions.
 */
typedef enum
{
    TRACE_CTX_SWITCH,
    TRACE_TASK_SLEEP,
    TRACE_TASK_WAKE,
    TRACE_TASK_STOP,
    TRACE_TASK_EXIT
} trace_event_type_t;

/*
 * Single trace event entry.
 *
 * Contains a timestamp (in scheduler ticks), the event type, and
 * optional task identifiers depending on the event.
 */
typedef struct
{
    uint64_t tick;
    trace_event_type_t type;
    int from_task;
    int to_task;
    int arg;
} trace_event_t;

void trace_record(trace_event_type_t type, int from_task, int to_task, int arg);
int trace_pop(trace_event_t *out);
void trace_clear(void);

#endif