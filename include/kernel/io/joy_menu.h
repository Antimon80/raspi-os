#ifndef KERNEL_IO_JOY_MENU_H
#define KERNEL_IO_JOY_MENU_H

typedef enum
{
    JOY_EVENT_NONE = 0,
    JOY_EVENT_UP,
    JOY_EVENT_DOWN,
    JOY_EVENT_LEFT,
    JOY_EVENT_RIGHT,
    JOY_EVENT_CENTER_PRESS,
    JOY_EVENT_CENTER_RELEASE
} joy_event_t;

void joy_menu_init(void);
void joy_menu_handle_event(joy_event_t event);

#endif