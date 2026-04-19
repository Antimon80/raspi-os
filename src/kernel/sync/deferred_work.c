#include "kernel/sync/deferred_work.h"
#include "kernel/irq.h"

#define DEFERRED_WORK_QUEUE_SIZE 32

typedef struct{
    deferred_work_fn_t fn;
    void *arg;
} deferred_work_item_t;

static deferred_work_item_t queue[DEFERRED_WORK_QUEUE_SIZE];
static volatile unsigned int head = 0;
static volatile unsigned int tail = 0;

static unsigned int next_index(unsigned int index){
    return (index + 1u) % DEFERRED_WORK_QUEUE_SIZE;
}

void deferred_work_init(void){
    head = 0;
    tail = 0;
}

int deferred_work_has_items(void){
    return head != tail;
}

int deferred_work_schedule(deferred_work_fn_t fn, void *arg){
    unsigned int next;

    if(!fn){
        return -1;
    }

    irq_disable();

    next = next_index(head);
    if(next == tail){
        irq_enable();
        return -1;
    }

    queue[head].fn = fn;
    queue[head].arg = arg;
    head = next;

    irq_enable();
    return 0;
}

int deferred_work_schedule_irq(deferred_work_fn_t fn, void *arg){
    unsigned next;
    
    if(!fn){
        return -1;
    }

    next = next_index(head);
    if(next == tail){
        return -1;
    }

    queue[head].fn = fn;
    queue[head].arg = arg;
    head = next;

    return 0;
}

void deferred_work_run_next(void){
    deferred_work_item_t item;

    irq_disable();

    if(head == tail){
        irq_enable();
        return;
    }

    item = queue[tail];
    tail = next_index(tail);

    irq_enable();

    item.fn(item.arg);
}