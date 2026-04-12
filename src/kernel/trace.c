#include "kernel/trace.h"
#include "kernel/timer.h"

#define TRACE_BUFFER_SIZE 128

static trace_event_t trace_buffer[TRACE_BUFFER_SIZE];
static volatile unsigned int trace_head = 0;
static volatile unsigned int trace_tail = 0;

/*
 * Record a trace event in the internal ring buffer.
 *
 * This function is designed to be lightweight and non-blocking.
 * If the buffer is full, the oldest entry is overwritten.
 */
void trace_record(trace_event_type_t type, int from_task, int to_task, int arg){
    unsigned int next = (trace_head + 1U) % TRACE_BUFFER_SIZE;

    if(next == trace_tail){
        trace_tail = (trace_tail + 1U) % TRACE_BUFFER_SIZE;
    }

    trace_buffer[trace_head].tick = timer_get_ticks();
    trace_buffer[trace_head].type = type;
    trace_buffer[trace_head].from_task = from_task;
    trace_buffer[trace_head].to_task = to_task;
    trace_buffer[trace_head].arg = arg;

    trace_head = next;
}

/*
 * Retrieve the next trace event from the buffer.
 *
 * Returns 0 on success, -1 if no event is available.
 */
int trace_pop(trace_event_t *out){
    if(!out){
        return -1;
    }

    if(trace_head == trace_tail){
        return -1;
    }

    *out = trace_buffer[trace_tail];
    trace_tail = (trace_tail + 1U) % TRACE_BUFFER_SIZE;
    return 0;
}

/*
 * Clear all recorded trace events.
 */
void trace_clear(void){
    trace_head = 0;
    trace_tail = 0;
}