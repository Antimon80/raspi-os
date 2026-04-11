#include "kernel/joy_menu.h"
#include "kernel/shell.h"
#include "kernel/timer.h"
#include "rpi4/uart.h"

#define JOY_MENU_ITEMS 4
#define JOY_LONG_PRESS_TICKS 50

typedef struct
{
    int active;
    int selected;
    int center_pressed;
    uint64_t center_press_tick;
} joy_menu_state_t;

static joy_menu_state_t menu_state;

static const char *menu_entries[JOY_MENU_ITEMS] =
{
    "help",
    "ps",
    "start demo",
    "stop demo"
};

static void joy_menu_render(void)
{
    uart_puts("\n[Joystick Menu]\n");

    for (int i = 0; i < JOY_MENU_ITEMS; i++)
    {
        if (i == menu_state.selected)
        {
            uart_puts("> ");
        }
        else
        {
            uart_puts("  ");
        }

        uart_puts(menu_entries[i]);
        uart_puts("\n");
    }
}

static void joy_menu_execute_selected(void)
{
    switch (menu_state.selected)
    {
    case 0:
        shell_cmd_help();
        break;

    case 1:
        shell_cmd_ps();
        break;

    case 2:
        shell_cmd_start_demo();
        break;

    case 3:
    {
        int id = shell_find_task_by_name("demo");

        if (id >= 0)
        {
            shell_cmd_stop_id(id);
        }
        else
        {
            uart_puts("demo task not running\n");
        }
        break;
    }

    default:
        break;
    }
}

void joy_menu_init(void)
{
    menu_state.active = 0;
    menu_state.selected = 0;
    menu_state.center_pressed = 0;
    menu_state.center_press_tick = 0;
}

void joy_menu_handle_event(joy_event_t event)
{
    uint64_t now;
    uint64_t held_ticks;

    switch (event)
    {
    case JOY_EVENT_CENTER_PRESS:
        menu_state.center_pressed = 1;
        menu_state.center_press_tick = timer_get_ticks();
        break;

    case JOY_EVENT_CENTER_RELEASE:
        if (!menu_state.center_pressed)
        {
            break;
        }

        menu_state.center_pressed = 0;
        now = timer_get_ticks();
        held_ticks = now - menu_state.center_press_tick;

        if (held_ticks >= JOY_LONG_PRESS_TICKS)
        {
            menu_state.active = !menu_state.active;

            if (menu_state.active)
            {
                uart_puts("\nmenu opened\n");
                joy_menu_render();
            }
            else
            {
                uart_puts("\nmenu closed\n");
            }
        }
        else
        {
            if (menu_state.active)
            {
                joy_menu_execute_selected();
                joy_menu_render();
            }
        }
        break;

    case JOY_EVENT_UP:
        if (menu_state.active)
        {
            if (menu_state.selected > 0)
            {
                menu_state.selected--;
            }
            joy_menu_render();
        }
        break;

    case JOY_EVENT_DOWN:
        if (menu_state.active)
        {
            if (menu_state.selected < (JOY_MENU_ITEMS - 1))
            {
                menu_state.selected++;
            }
            joy_menu_render();
        }
        break;

    case JOY_EVENT_LEFT:
    case JOY_EVENT_RIGHT:
    case JOY_EVENT_NONE:
    default:
        break;
    }
}