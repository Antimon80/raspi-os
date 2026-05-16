#include "kernel/tasks/demo_task.h"
#include "kernel/sched/scheduler.h"
#include "kernel/memory/log.h"

void heartbeat_task(void)
{
    int counter = 0;

    log_append_current_task("heartbeat task started ", counter);

    while (1)
    {
        counter++;

        if (counter == 1)
        {
            log_append_current_task("heartbeat loop entered ", counter);
        }

        if ((counter % 10) == 0)
        {
            log_append_current_task("heartbeat: ", counter);
        }

        task_sleep(100);
    }
}

void worker_fast_task(void)
{
    int counter = 0;

    while (1)
    {
        for (volatile int i = 0; i < 10000; i++)
        {
            counter++;

            if (counter % 50 == 0)
            {
                log_append_current_task("worker_fast: ", counter);
            }

            scheduler_yield();
        }
    }
}

void worker_slow_task(void)
{
    int counter = 0;

    while (1)
    {
        for (volatile int i = 0; i < 100000; i++)
        {
            counter++;

            if (counter % 10 == 0)
            {
                log_append_current_task("worker_slow: ", counter);
            }

            if (counter % 20 == 0)
            {
                scheduler_yield();
            }
        }
    }
}

void burst_task(void)
{
    int round = 0;

    while (1)
    {
        round++;

        log_append_current_task("burst: start round ", round);

        for (int i = 0; i < 5; i++)
        {
            for (volatile int j = 0; j < 50000; j++)
            {
                        }
            log_append_current_task("burst: sleep ", 0);
            task_sleep(200);
        }
    }
}