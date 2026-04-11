#include "kernel/joy_menu.h"
#include "kernel/shell.h"
#include "kernel/timer.h"
#include "rpi4/uart.h"

#define JOY_MENU_ITEMS 4
#define JOY_LONG_PRESS_TICKS 50

/*
 * Internal state of the joystick-controlled menu.
 *
 * active               - whether the menu is currently visible
 * selected             - index of the currently selected entry
 * center_pressed       - flag indicating that the center button is held down
 * center_press_tick    - timestamp when the center button was pressed
 */
typedef struct
{
    int active;
    int selected;
    int center_pressed;
    uint64_t center_press_tick;
} joy_menu_state_t;

/* Global menu state (single instance) */
static joy_menu_state_t menu_state;

/*
 * Static list of menu entries.
 *
 * Each entry corresponds to a shell command that will be executed
 * when the user selects it and performs a short press.
 */
static const char *menu_entries[JOY_MENU_ITEMS] =
    {
        "help",
        "ps",
        "start demo",
        "stop demo"};

/*
 * Render the joystick menu to the UART console.
 *
 * The currently selected entry is highlighted with a '>' marker.
 */
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

/*
 * Execute the currently selected menu entry.
 *
 * This maps menu indices to corresponding shell commands.
 */
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

/*
 * Initialize the joystick menu state.
 *
 * The menu starts inactive and with the first entry selected.
 */
void joy_menu_init(void)
{
    menu_state.active = 0;
    menu_state.selected = 0;
    menu_state.center_pressed = 0;
    menu_state.center_press_tick = 0;
}

/*
 * Handle joystick events and update menu state accordingly.
 *
 * Event handling logic:
 *
 * - CENTER_PRESS:
 *     Start measuring how long the button is held.
 *
 * - CENTER_RELEASE:
 *     Distinguish between:
 *       * long press  → toggle menu open/close
 *       * short press → execute selected entry (if menu is active)
 *
 * - UP / DOWN:
 *     Navigate through menu entries (only if menu is active).
 *
 * - LEFT / RIGHT:
 *     Currently unused.
 */
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