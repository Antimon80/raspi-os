#include "kernel/io/joy_menu.h"
#include "kernel/io/shell.h"
#include "kernel/io/console.h"
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
    uint64_t render_count;
} joy_menu_state_t;

/* Global menu state (single instance) */
static joy_menu_state_t menu_state;

static void joy_menu_mark_rendered(void)
{
    menu_state.render_count = console_get_write_counter();
}

static int joy_menu_console_changed_since_render(void)
{
    return menu_state.render_count != console_get_write_counter();
}

/*
 * Static list of menu entries.
 *
 * Each entry contains both the visible label and the function that
 * should be executed when selected.
 */
static const char *menu_entries[] =
    {
        "help",
        "ps",
        "start env",
        "stop env",
        "start envled",
        "stop envled",
        "start tictactoe",
        "stop tictactoe",
        "start heart",
        "stop heart",
        "start fast",
        "stop fast",
        "start slow",
        "stop slow",
        "start gol",
        "stop gol",
        "env history",
        "trace dump",
};

#define JOY_MENU_ITEMS ((int)(sizeof(menu_entries) / sizeof(menu_entries[0])))

/*
 * Execute the currently selected menu entry.
 */
static void joy_menu_execute_selected(void)
{
    if (menu_state.selected < 0 || menu_state.selected >= JOY_MENU_ITEMS)
    {
        return;
    }

    shell_execute_command(menu_entries[menu_state.selected]);
}

/*
 * Move the terminal cursor up by the given number of rows.
 */
static void joy_menu_cursor_up(unsigned int rows)
{
    if (rows == 0)
    {
        return;
    }

    console_puts("\x1b[");
    console_put_uint(rows);
    console_puts("A");
}

/*
 * Move the terminal cursor down by the given number of rows.
 */
static void joy_menu_cursor_down(unsigned int rows)
{
    if (rows == 0)
    {
        return;
    }

    console_puts("\x1b[");
    console_put_uint(rows);
    console_puts("B");
}

/*
 * Move the terminal cursor to the start of the current line.
 */
static void joy_menu_cursor_line_start(void)
{
    console_puts("\r");
}

/*
 * Clear the current terminal line.
 */
static void joy_menu_clear_line(void)
{
    console_puts("\x1b[2K");
}

/*
 * Render one menu line with or without the selection marker.
 */
static void joy_menu_render_item(int index, int selected)
{
    if (index < 0 || index >= JOY_MENU_ITEMS)
    {
        return;
    }

    joy_menu_cursor_line_start();
    joy_menu_clear_line();

    if (selected)
    {
        console_puts("> ");
    }
    else
    {
        console_puts("  ");
    }

    console_puts(menu_entries[index]);
}

/*
 * Render the joystick menu to the UART console.
 *
 * The currently selected entry is highlighted with a '>' marker.
 */
static void joy_menu_render(void)
{
    console_puts("\n[Joystick Menu]\n");

    for (int i = 0; i < JOY_MENU_ITEMS; i++)
    {
        if (i == menu_state.selected)
        {
            console_puts("> ");
        }
        else
        {
            console_puts("  ");
        }

        console_puts(menu_entries[i]);
        if (i < (JOY_MENU_ITEMS - 1))
        {
            console_puts("\n");
        }
    }

    joy_menu_mark_rendered();
}

/*
 * Update only the old and the new selected menu line.
 *
 * This assumes that no other task has written to the console between the last
 * menu render/update and this update. If other output appears, call
 * joy_menu_render() again to resynchronize the display.
 */
static void joy_menu_update_selection(int old_selected, int new_selected)
{
    int current_line_from_menu_end;
    int old_line_from_menu_end;
    int new_line_from_menu_end;

    if (old_selected == new_selected)
    {
        return;
    }

    if (joy_menu_console_changed_since_render())
    {
        joy_menu_render();
        return;
    }

    old_line_from_menu_end = (JOY_MENU_ITEMS - 1) - old_selected;
    new_line_from_menu_end = (JOY_MENU_ITEMS - 1) - new_selected;

    joy_menu_cursor_up((unsigned int)old_line_from_menu_end);
    joy_menu_render_item(old_selected, 0);

    current_line_from_menu_end = old_line_from_menu_end;

    if (new_line_from_menu_end > current_line_from_menu_end)
    {
        joy_menu_cursor_up((unsigned int)(new_line_from_menu_end - current_line_from_menu_end));
    }
    else if (new_line_from_menu_end < current_line_from_menu_end)
    {
        joy_menu_cursor_down((unsigned int)(current_line_from_menu_end - new_line_from_menu_end));
    }

    joy_menu_render_item(new_selected, 1);

    joy_menu_cursor_down((unsigned int)new_line_from_menu_end);
    joy_menu_cursor_line_start();

    joy_menu_mark_rendered();
}

static void joy_menu_move_selection(int delta)
{
    int old_selected = menu_state.selected;
    int new_selected = menu_state.selected + delta;

    if (new_selected < 0)
    {
        new_selected = 0;
    }

    if (new_selected >= JOY_MENU_ITEMS)
    {
        new_selected = JOY_MENU_ITEMS - 1;
    }

    if (new_selected == old_selected)
    {
        return;
    }

    menu_state.selected = new_selected;
    joy_menu_update_selection(old_selected, new_selected);
}

/*
 * Initialize the joystick menu state.
 *
 * The menu starts inactive and with the first entry selected.
 */
void joy_menu_init(void)
{
    menu_state.active = 1;
    menu_state.selected = 0;
    menu_state.center_pressed = 0;
    menu_state.center_press_tick = 0;
    menu_state.render_count = console_get_write_counter();

    joy_menu_render();
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
                console_puts("\nmenu opened\n");
                joy_menu_render();
            }
            else
            {
                console_puts("\nmenu closed\n");
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
            joy_menu_move_selection(-1);
        }
        break;

    case JOY_EVENT_DOWN:
        if (menu_state.active)
        {
            joy_menu_move_selection(1);
        }
        break;

    case JOY_EVENT_LEFT:
    case JOY_EVENT_RIGHT:
    case JOY_EVENT_NONE:
    default:
        break;
    }
}
