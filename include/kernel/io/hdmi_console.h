#ifndef KERNEL_IO_HDMI_CONSOLE_H
#define KERNEL_IO_HDMI_CONSOLE_H

void hdmi_console_init(void);
void hdmi_console_register_task_id(int id);
void hdmi_console_enable(int enabled);
int hdmi_console_is_enabled(void);
int hdmi_console_enqueue(char c);
void hdmi_console_task(void);

#endif