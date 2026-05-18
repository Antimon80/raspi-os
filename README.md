# Raspberry Pi Bare-Metal OS

A small bare-metal operating system for the Raspberry Pi 4 with cooperative multitasking, UART shell interaction, HDMI output, Sense HAT support, and basic diagnostics.

## Features

### Cooperative Scheduler

The kernel runs multiple tasks using a cooperative scheduler. Tasks explicitly yield or sleep, and the scheduler manages task states such as ready, running, sleeping, blocked, and dying.

Built-in task examples include:

Built-in task examples include:

- `heart`  
  A simple heartbeat task that periodically prints or signals that the scheduler is alive.

- `fast`  
  A demo worker task that runs frequently and yields cooperatively. Useful for testing scheduler behavior under constant runnable load.

- `slow`  
  A slower demo worker task used to compare scheduling behavior against faster or more active tasks.

- `burst`  
  A demo task that produces bursts of output or activity. Useful for testing console output, logging, and scheduler responsiveness.

- `gol`  
  Runs Conway's Game of Life on the Sense HAT LED matrix.

- `tictactoe`  
  Runs a small Tic-Tac-Toe game using HDMI output, Sense HAT joystick input, and optionally the LED matrix.

- `env`  
  Reads environment sensor data from the Sense HAT and stores the latest sample plus a small history buffer.

- `envled`  
  Displays a compact temperature and humidity status visualization on the Sense HAT LED matrix.

- `envdash`  
  Shows a live HDMI dashboard with converted environment sensor values and a legend explaining the LED matrix colors.

- `diagdash`  
  Shows a live HDMI diagnostics dashboard with CPU activity, heap usage, and per-task stack usage.

---

### UART Shell

The UART shell is the main control interface. It supports commands for starting and stopping tasks, inspecting task state, reading logs, dumping traces, and showing system diagnostics.

---

### HDMI Output

HDMI output uses a firmware-allocated framebuffer and a simple text renderer.

The display is split into two panes:

- **Main pane**: shell output, dashboards, and application views
- **Menu pane**: joystick-controlled menu

The main pane can be acquired exclusively by tasks such as Tic-Tac-Toe, the environment dashboard, or the diagnostics dashboard. While a task owns the main pane, normal shell mirroring stays on UART only.

---

### Joystick Menu

The Sense HAT joystick can be used to control the system without relying on the UART shell.

The menu supports:

- Running common shell commands
- Viewing task logs
- Starting and stopping tasks
- Opening dashboards

---

### Sense HAT LED Matrix

The LED matrix is managed through a dedicated LED render task. Tasks submit frames instead of writing to the matrix directly, preventing concurrent writers from corrupting the display.

Supported visual tasks include:

- Game of Life
- Tic-Tac-Toe
- Environment status display

---

### Environment Sensors

The environment task reads Sense HAT sensor data and stores the latest sample plus a small history buffer.

Available views:

- `env` prints the latest sample to the UART shell
- `env history` prints recent samples
- `envled` shows temperature and humidity status on the LED matrix
- `envdash` shows a live HDMI dashboard with sensor values and LED color legend

---

### Diagnostics

The `diag` command prints system diagnostics through the UART shell.

It includes:

- Uptime
- CPU busy percentage
- Heap usage
- Heap block statistics
- Per-task stack high-water usage
- Scheduler runtime counters

The `diagdash` task displays the same kind of information live on the HDMI main pane.

---

### Shutdown

The `shutdown` command performs a controlled kernel halt:

- Releases task-owned display resources
- Clears the LED matrix directly
- Clears HDMI panes
- Prints a shutdown message
- Disables interrupts
- Parks the CPU in a `wfi` loop

---

## Disclaimer

Tis OS was developed as a project in the Bachelor's degree programme in Computer Science at the University of Basel. It is for demonstration purposes only. The code is provided without guarantees. There is no claim to function, availability and support.


---

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.