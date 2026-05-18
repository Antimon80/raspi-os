#include "kernel/shutdown.h"
#include "kernel/irq.h"
#include "kernel/io/console.h"
#include "kernel/io/hdmi_console.h"
#include "kernel/tasks/tictactoe_task.h"
#include "kernel/tasks/env_dash_task.h"
#include "kernel/tasks/diag_dash_task.h"
#include "kernel/tasks/env_status_task.h"
#include "kernel/tasks/gol_task.h"
#include "kernel/tasks/env_task.h"
#include "kernel/tasks/led_task.h"
#include "sensehat/led_matrix.h"
#include "rpi4/hdmi/hdmi.h"

/*
 * Stop the kernel in a controlled state.
 *
 * Releases task-owned devices, clears shared displays where possible,
 * disables IRQs and parks the CPU in a low-activity WFI loop.
 */
void kernel_shutdown(void)
{
    console_puts("\nshutdown: stopping system\n");

    // Release application-owned HDMI/LED resources first.
    diag_dash_cleanup_resources();
    env_dash_cleanup_resources();
    ttt_cleanup_resources();

    if (env_status_get_task_id() >= 0)
    {
        led_submit_clear_frame(env_status_get_task_id());
        led_release(env_status_get_task_id());
        env_status_set_task_id(-1);
    }

    if (gol_get_task_id() >= 0)
    {
        led_release(gol_get_task_id());
        gol_set_task_id(-1);
    }

    if (env_get_task_id() >= 0)
    {
        env_set_running(0);
    }

    // Stop further HDMI mirroring and clear stale queued mirror output.
    hdmi_console_enable(0);
    hdmi_console_clear_queue();

    if (hdmi_is_available())
    {
        hdmi_clear_pane(HDMI_PANE_MAIN);
        hdmi_clear_pane(HDMI_PANE_MENU);

        hdmi_write_line(HDMI_PANE_MAIN, 0u, 0x00FFCE54u, 0x00161F2Au, "SYSTEM HALTED");
        hdmi_write_line(HDMI_PANE_MAIN, 2u, 0x00F2F6F8u, 0x00161F2Au, "It is now safe to remove power.");

        while (hdmi_present(32u))
        {
        }
    }

    if (led_matrix_clear_shutdown() < 0)
    {
        console_puts("shutdown: failed to clear LED matrix\n");
    }

    console_puts("shutdown: system halted\n");

    irq_disable();

    while (1)
    {
        asm volatile("wfi");
    }
}