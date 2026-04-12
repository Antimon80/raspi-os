#include "kernel/shell/joy_menu.h"
#include "kernel/shell/shell.h"
#include "kernel/timer.h"
#include "rpi4/uart.h"

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

/*
 * One menu entry consists of the visible label and the action that
 * is executed when the entry is selected.
 */
typedef struct
{
    const char *label;
    void (*action)(void);
} joy_menu_entry_t;

/* Global menu state (single instance) */
static joy_menu_state_t menu_state;

/*
 * Menu action wrappers.
 *
 * The joystick menu cannot accept free-text input, so task-specific
 * actions are exposed as explicit menu entries.
 */
static void joy_menu_cmd_help(void)
{
    shell_cmd_help();
}

static void joy_menu_cmd_ps(void)
{
    shell_cmd_ps();
}

static void joy_menu_cmd_start_demo(void)
{
    shell_cmd_start_arg("demo");
}

static void joy_menu_cmd_stop_demo(void)
{
    shell_cmd_stop_arg("demo");
}

/*
 * Static list of menu entries.
 *
 * Each entry contains both the visible label and the function that
 * should be executed when selected.
 */
static const joy_menu_entry_t menu_entries[] =
{
    { "help",       joy_menu_cmd_help },
    { "ps",         joy_menu_cmd_ps },
    { "start demo", joy_menu_cmd_start_demo },
    { "stop demo",  joy_menu_cmd_stop_demo }
};

#define JOY_MENU_ITEMS ((int)(sizeof(menu_entries) / sizeof(menu_entries[0])))

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

        uart_puts(menu_entries[i].label);
        uart_puts("\n");
    }
}

/*
 * Execute the currently selected menu entry.
 */
static void joy_menu_execute_selected(void)
{
    if (menu_state.selected < 0 || menu_state.selected >= JOY_MENU_ITEMS)
    {
        return;
    }

    if (menu_entries[menu_state.selected].action)
    {
        menu_entries[menu_state.selected].action();
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
 *       * long press  -> toggle menu open/close
 *       * short press -> execute selected entry (if menu is active)
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