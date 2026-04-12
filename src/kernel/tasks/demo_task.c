#include "kernel/tasks/demo_task.h"
#include "kernel/memory/log.h"
#include "kernel/sched/scheduler.h"

/*
 * Simple demo task.
 * Periodically prints a message so that task switching becomes visible
 * on the UART output.
 */
void demo_task(void)
{
    int counter = 0;

    log_append_current_task("demo task started");

    while (1)
    {
        counter++;

        if (counter == 1)
        {
            log_append_current_task("demo loop entered");
        }

        if ((counter % 5) == 0)
        {
            log_append_current_task("demo heartbeat");
        }

        task_sleep(100);
    }
}