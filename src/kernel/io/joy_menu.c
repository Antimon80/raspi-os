#include "kernel/io/joy_menu.h"
#include "kernel/io/shell.h"
#include "kernel/io/console.h"
#include "kernel/timer.h"
#include "rpi4/hdmi/hdmi.h"
#include "util/string.h"

#define JOY_LONG_PRESS_TICKS 50

#define JOY_MENU_BG 0x00161F2Au
#define JOY_MENU_TITLE 0x0098F4FFu
#define JOY_MENU_LINE 0x0028C7FAu
#define JOY_MENU_TEXT 0x00F2F6F8u
#define JOY_MENU_MUTED 0x0087A3B7u
#define JOY_MENU_SELECTED 0x00FFCE54u
#define JOY_MENU_HINT 0x0041E4FFu

#define JOY_MENU_LINE_WIDTH 24u
#define JOY_MENU_FIRST_ITEM_ROW 5u
#define JOY_MENU_MAX_VISIBLE_ITEMS 18u
#define JOY_MENU_COMMAND_BUFFER_SIZE 64u

typedef enum
{
    JOY_MENU_VIEW_MAIN = 0,
    JOY_MENU_VIEW_LOG,
    JOY_MENU_VIEW_TASKS
} joy_menu_view_t;

typedef enum
{
    JOY_MENU_ACTION_NONE = 0,
    JOY_MENU_ACTION_COMMAND,
    JOY_MENU_ACTION_OPEN_LOG,
    JOY_MENU_ACTION_OPEN_TASKS,
    JOY_MENU_ACTION_ENV_DASH,
    JOY_MENU_ACTION_HELP
} joy_menu_action_t;

typedef struct
{
    const char *label;
    joy_menu_action_t action;
} joy_menu_main_entry_t;

typedef struct
{
    int active;
    joy_menu_view_t view;

    int main_selected;
    int log_selected;
    int task_selected;

    int center_pressed;
    uint64_t center_press_tick;
} joy_menu_state_t;

static joy_menu_state_t menu_state;

static const joy_menu_main_entry_t main_entries[] =
    {
        {"env live", JOY_MENU_ACTION_ENV_DASH},
        {"env history", JOY_MENU_ACTION_COMMAND},
        {"help", JOY_MENU_ACTION_HELP},
        {"heap stats", JOY_MENU_ACTION_COMMAND},
        {"log", JOY_MENU_ACTION_OPEN_LOG},
        {"ps", JOY_MENU_ACTION_COMMAND},
        {"tasks", JOY_MENU_ACTION_OPEN_TASKS},
        {"trace dump", JOY_MENU_ACTION_COMMAND},
};

static const char *log_entries[] = {
    "burst",
    "env",
    "envled",
    "fast",
    "gol",
    "hdmi_console",
    "heart",
    "joystick",
    "shell",
    "slow",
    "tictactoe",
};

static const char *task_entries[] = {
    "burst",
    "env",
    "envled",
    "fast",
    "gol",
    "heart",
    "slow",
    "tictactoe",
};

#define JOY_MENU_MAIN_ITEMS ((int)(sizeof(main_entries) / sizeof(main_entries[0])))
#define JOY_MENU_LOG_ITEMS ((int)(sizeof(log_entries) / sizeof(log_entries[0])))
#define JOY_MENU_TASK_ITEMS ((int)(sizeof(task_entries) / sizeof(task_entries[0])))

static int joy_menu_current_count(void)
{
    if (menu_state.view == JOY_MENU_VIEW_LOG)
    {
        return JOY_MENU_LOG_ITEMS;
    }

    if (menu_state.view == JOY_MENU_VIEW_TASKS)
    {
        return JOY_MENU_TASK_ITEMS;
    }

    return JOY_MENU_MAIN_ITEMS;
}

static int *joy_menu_current_selection(void)
{
    if (menu_state.view == JOY_MENU_VIEW_LOG)
    {
        return &menu_state.log_selected;
    }

    if (menu_state.view == JOY_MENU_VIEW_TASKS)
    {
        return &menu_state.task_selected;
    }

    return &menu_state.main_selected;
}

static const char *joy_menu_current_label(int index)
{
    if (menu_state.view == JOY_MENU_VIEW_LOG)
    {
        return log_entries[index];
    }

    if (menu_state.view == JOY_MENU_VIEW_TASKS)
    {
        return task_entries[index];
    }

    return main_entries[index].label;
}

static void joy_menu_build_prefixed_command(char *out, unsigned int out_size, const char *prefix, const char *name)
{
    unsigned int pos = 0u;

    if (!out || out_size == 0u)
    {
        return;
    }

    while (prefix && *prefix && pos + 1u < out_size)
    {
        out[pos++] = *prefix++;
    }

    while (name && *name && pos + 1u < out_size)
    {
        out[pos++] = *name++;
    }

    out[pos] = 0;
}

static void joy_menu_hdmi_puts(uint32_t fg, const char *s)
{
    hdmi_set_text_colors(HDMI_PANE_MENU, fg, JOY_MENU_BG);
    hdmi_puts(HDMI_PANE_MENU, s);
    hdmi_reset_text_colors(HDMI_PANE_MENU);
}

static void joy_menu_pad_line(unsigned int used)
{
    hdmi_set_text_colors(HDMI_PANE_MENU, JOY_MENU_TEXT, JOY_MENU_BG);

    while (used < JOY_MENU_LINE_WIDTH)
    {
        hdmi_putc(HDMI_PANE_MENU, ' ');
        used++;
    }

    hdmi_reset_text_colors(HDMI_PANE_MENU);
}

static void joy_menu_write_line(uint32_t row, uint32_t fg, const char *s)
{
    unsigned int used = (unsigned int)str_length(s);

    hdmi_set_cursor(HDMI_PANE_MENU, 0u, row);
    hdmi_set_text_colors(HDMI_PANE_MENU, fg, JOY_MENU_BG);
    hdmi_puts(HDMI_PANE_MENU, s);
    hdmi_reset_text_colors(HDMI_PANE_MENU);

    joy_menu_pad_line(used);
}

static void joy_menu_clear_from(uint32_t row)
{
    while (row < 28u)
    {
        joy_menu_write_line(row, JOY_MENU_TEXT, "");
        row++;
    }
}

static void joy_menu_write_help(void)
{
    console_puts("\nJoystick menu help\n");
    console_puts("------------------\n");
    console_puts("Long CENTER: open or close the current menu level\n");
    console_puts("UP/DOWN: move selection\n");
    console_puts("Short CENTER in main menu: execute entry or open submenu\n");
    console_puts("Short CENTER in log menu: print selected task log\n");
    console_puts("LEFT in tasks menu: start selected task\n");
    console_puts("RIGHT in tasks menu: stop selected task\n");
    console_puts("Shell output is printed on UART and, if free, on the HDMI main pane.\n");
}

static void joy_menu_execute_log_selected(void)
{
    char command[JOY_MENU_COMMAND_BUFFER_SIZE];

    if (menu_state.log_selected < 0 || menu_state.log_selected >= JOY_MENU_LOG_ITEMS)
    {
        return;
    }

    joy_menu_build_prefixed_command(command, sizeof(command), "log ", log_entries[menu_state.log_selected]);

    shell_execute_command(command);
}

static void joy_menu_start_task_selected(void)
{
    char command[JOY_MENU_COMMAND_BUFFER_SIZE];

    if (menu_state.task_selected < 0 || menu_state.task_selected >= JOY_MENU_TASK_ITEMS)
    {
        return;
    }

    joy_menu_build_prefixed_command(command, sizeof(command), "start ", task_entries[menu_state.task_selected]);

    shell_execute_command(command);
}

static void joy_menu_stop_task_selected(void)
{
    char command[JOY_MENU_COMMAND_BUFFER_SIZE];

    if (menu_state.task_selected < 0 || menu_state.task_selected >= JOY_MENU_TASK_ITEMS)
    {
        return;
    }

    joy_menu_build_prefixed_command(command, sizeof(command), "stop ", task_entries[menu_state.task_selected]);

    shell_execute_command(command);
}

static void joy_menu_execute_main_selected(void)
{
    const joy_menu_main_entry_t *entry;

    if (menu_state.main_selected < 0 || menu_state.main_selected >= JOY_MENU_MAIN_ITEMS)
    {
        return;
    }

    entry = &main_entries[menu_state.main_selected];

    switch (entry->action)
    {
    case JOY_MENU_ACTION_COMMAND:
        shell_execute_command(entry->label);
        break;
    case JOY_MENU_ACTION_OPEN_LOG:
        menu_state.view = JOY_MENU_VIEW_LOG;
        menu_state.log_selected = 0;
        break;
    case JOY_MENU_ACTION_OPEN_TASKS:
        menu_state.view = JOY_MENU_VIEW_TASKS;
        menu_state.task_selected = 0;
        break;
    case JOY_MENU_ACTION_ENV_DASH:
        console_puts("env dash: LEFT starts dashboard, RIGHT stops dashboard\n");
        break;
    case JOY_MENU_ACTION_HELP:
        joy_menu_write_help();
        break;
    case JOY_MENU_ACTION_NONE:
    default:
        break;
    }
}

static const char *joy_menu_title(void)
{
    if (menu_state.view == JOY_MENU_VIEW_LOG)
    {
        return "LOGS";
    }

    if (menu_state.view == JOY_MENU_VIEW_TASKS)
    {
        return "TASKS";
    }

    return "JOYSTICK MENU";
}

static const char *joy_menu_hint_line_1(void)
{
    if (menu_state.view == JOY_MENU_VIEW_LOG)
    {
        return "CENTER: print log";
    }

    if (menu_state.view == JOY_MENU_VIEW_TASKS)
    {
        return "LEFT start / RIGHT stop";
    }

    return "CENTER: select";
}

static const char *joy_menu_hint_line_2(void)
{
    if (menu_state.view == JOY_MENU_VIEW_LOG)
    {
        return "LONG CENTER: back";
    }

    if (menu_state.view == JOY_MENU_VIEW_TASKS)
    {
        return "LONG CENTER: back";
    }

    return "LONG CENTER: close";
}

static void joy_menu_render(void)
{
    int selected;
    int count;
    int first_visible = 0;
    int last_visible;
    int visible_count;

    if (!hdmi_is_available())
    {
        return;
    }

    hdmi_clear_pane(HDMI_PANE_MENU);

    joy_menu_write_line(0u, JOY_MENU_TITLE, joy_menu_title());
    joy_menu_write_line(1u, JOY_MENU_LINE, "============");
    joy_menu_write_line(2u, JOY_MENU_HINT, joy_menu_hint_line_1());
    joy_menu_write_line(3u, JOY_MENU_MUTED, joy_menu_hint_line_2());

    if (!menu_state.active)
    {
        joy_menu_write_line(5u, JOY_MENU_MUTED, "menu closed");
        joy_menu_write_line(7u, JOY_MENU_MUTED, "long center");
        joy_menu_write_line(8u, JOY_MENU_MUTED, "to open");
        joy_menu_clear_from(10u);

        while (hdmi_present(16u))
        {
        }

        return;
    }

    selected = *joy_menu_current_selection();
    count = joy_menu_current_count();

    if (selected >= (int)JOY_MENU_MAX_VISIBLE_ITEMS)
    {
        first_visible = selected - (int)JOY_MENU_MAX_VISIBLE_ITEMS + 1;
    }

    last_visible = first_visible + (int)JOY_MENU_MAX_VISIBLE_ITEMS;
    if (last_visible > count)
    {
        last_visible = count;
    }

    visible_count = last_visible - first_visible;

    for (int i = 0; i < visible_count; i++)
    {
        int entry_index = first_visible + i;
        const char *label = joy_menu_current_label(entry_index);
        uint32_t row = JOY_MENU_FIRST_ITEM_ROW + (uint32_t)i;
        unsigned int used;

        hdmi_set_cursor(HDMI_PANE_MENU, 0u, row);

        if (entry_index == selected)
        {
            joy_menu_hdmi_puts(JOY_MENU_SELECTED, "> ");
            joy_menu_hdmi_puts(JOY_MENU_SELECTED, label);
        }
        else
        {
            joy_menu_hdmi_puts(JOY_MENU_MUTED, "  ");
            joy_menu_hdmi_puts(JOY_MENU_TEXT, label);
        }

        used = 2u + (unsigned)str_length(label);
        joy_menu_pad_line(used);
    }

    if (last_visible < count)
    {
        joy_menu_write_line(JOY_MENU_FIRST_ITEM_ROW + (uint32_t)visible_count, JOY_MENU_MUTED, "... more below");
        joy_menu_clear_from(JOY_MENU_FIRST_ITEM_ROW + (uint32_t)visible_count + 1u);
    }
    else
    {
        joy_menu_clear_from(JOY_MENU_FIRST_ITEM_ROW + (uint32_t)visible_count);
    }

    while (hdmi_present(16u))
    {
    }
}

static void joy_menu_move_selection(int delta)
{
    int *selected = joy_menu_current_selection();
    int count = joy_menu_current_count();
    int new_selected;

    if (!selected || count <= 0)
    {
        return;
    }

    new_selected = *selected + delta;

    if (new_selected < 0)
    {
        new_selected = 0;
    }

    if (new_selected >= count)
    {
        new_selected = count - 1;
    }

    if (new_selected == *selected)
    {
        return;
    }

    *selected = new_selected;
    joy_menu_render();
}

static void joy_menu_close_current_level(void)
{
    if (menu_state.view == JOY_MENU_VIEW_MAIN)
    {
        menu_state.active = !menu_state.active;
        joy_menu_render();

        if (menu_state.active)
        {
            console_puts("menu opened\n");
        }
        else
        {
            console_puts("menu closed\n");
        }

        return;
    }

    menu_state.view = JOY_MENU_VIEW_MAIN;
    joy_menu_render();
}

void joy_menu_init(void)
{
    menu_state.active = 1;
    menu_state.view = JOY_MENU_VIEW_MAIN;

    menu_state.main_selected = 0;
    menu_state.log_selected = 0;
    menu_state.task_selected = 0;

    menu_state.center_pressed = 0;
    menu_state.center_press_tick = 0;

    joy_menu_render();
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
            joy_menu_close_current_level();
            break;
        }

        if (!menu_state.active)
        {
            break;
        }

        if (menu_state.view == JOY_MENU_VIEW_MAIN)
        {
            joy_menu_execute_main_selected();
            joy_menu_render();
        }
        else if (menu_state.view == JOY_MENU_VIEW_LOG)
        {
            joy_menu_execute_log_selected();
            joy_menu_render();
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
        if (menu_state.active && menu_state.view == JOY_MENU_VIEW_TASKS)
        {
            joy_menu_start_task_selected();
            joy_menu_render();
        }
        break;
    case JOY_EVENT_RIGHT:
        if (menu_state.active && menu_state.view == JOY_MENU_VIEW_TASKS)
        {
            joy_menu_stop_task_selected();
            joy_menu_render();
        }
        break;
    case JOY_EVENT_NONE:
    default:
        break;
    }
}