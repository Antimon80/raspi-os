#include "kernel/tasks/gol_task.h"
#include "kernel/tasks/led_task.h"
#include "kernel/timer.h"
#include "kernel/io/console.h"
#include "kernel/sched/task.h"
#include "kernel/sched/scheduler.h"
#include "rpi4/uart.h"
#include <stdint.h>

#define WIDTH 8
#define HEIGHT 8
#define FRAME_DELAY_TICKS 12
#define MAX_AGE 7
#define RESEED_AFTER_FRAMES 120

static int gol_task_id = -1;

/*
 * A cell is alive if age > 0.
 * Age is used both for state and for color rendering.
 */
static uint8_t current[HEIGHT][WIDTH];
static uint8_t next[HEIGHT][WIDTH];

/*
 * Very small PRNG state for demo reseeding.
 */
static uint32_t rng_state = 0x12345678u;
static uint32_t generation_count = 0;

/*
 * Return a pseudo-random 32-bit value.
 */
static uint32_t gol_rand(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

/*
 * Return x modulo width and y modulo height for toroidal wrap-around.
 */
static int gol_wrap_x(int x)
{
    while (x < 0)
    {
        x += WIDTH;
    }

    while (x >= WIDTH)
    {
        x -= WIDTH;
    }

    return x;
}

static int gol_wrap_y(int y)
{
    while (y < 0)
    {
        y += HEIGHT;
    }

    while (y >= HEIGHT)
    {
        y -= HEIGHT;
    }

    return y;
}

/*
 * Return non-zero if the given cell is alive.
 */
static int gol_is_alive(uint8_t age)
{
    return age > 0;
}

/*
 * Count live neighbors using toroidal wrap-around.
 */
static int gol_count_neighbors(int x, int y)
{
    int count = 0;

    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            int nx;
            int ny;

            if (dx == 0 && dy == 0)
            {
                continue;
            }

            nx = gol_wrap_x(x + dx);
            ny = gol_wrap_y(y + dy);

            if (gol_is_alive(current[ny][nx]))
            {
                count++;
            }
        }
    }

    return count;
}

/*
 * Clear a grid to all-dead.
 */
static void gol_clear_grid(uint8_t grid[HEIGHT][WIDTH])
{
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            grid[y][x] = 0;
        }
    }
}

/*
 * Copy next generation into current generation.
 */
static void gol_copy_next_to_current(void)
{
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            current[y][x] = next[y][x];
        }
    }
}

/*
 * Return non-zero if both grids are identical.
 */
static int gol_grids_equal(uint8_t a[HEIGHT][WIDTH], uint8_t b[HEIGHT][WIDTH])
{
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            if (a[y][x] != b[y][x])
            {
                return 0;
            }
        }
    }

    return 1;
}

/*
 * Return non-zero if there is at least one live cell.
 */
static int gol_any_alive(void)
{
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            if (gol_is_alive(current[y][x]))
            {
                return 1;
            }
        }
    }

    return 0;
}

/*
 * Seed a glider.
 */
static void gol_seed_glider(void)
{
    current[1][2] = 1;
    current[2][3] = 1;
    current[3][1] = 1;
    current[3][2] = 1;
    current[3][3] = 1;
}

/*
 * Seed a small exploder-like pattern.
 */
static void gol_seed_explorer(void)
{
    current[2][3] = 1;
    current[1][2] = 1;
    current[1][3] = 1;
    current[1][4] = 1;
    current[3][2] = 1;
    current[3][4] = 1;
    current[4][3] = 1;
}

/*
 * Seed a lightweight spaceship-ish pattern adapted to 8x8.
 */
static void gol_seed_ship(void)
{
    current[1][2] = 1;
    current[1][5] = 1;
    current[2][1] = 1;
    current[3][1] = 1;
    current[3][5] = 1;
    current[4][1] = 1;
    current[4][2] = 1;
    current[4][3] = 1;
    current[4][4] = 1;
}

/*
 * Seed a random board.
 */
static void gol_seed_random(void)
{
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            if (((gol_rand() >> 16) % 3u) == 0u)
            {
                current[y][x] = 1;
            }
        }
    }
}

/*
 * Choose one of several start patterns.
 */
static void gol_seed_pattern(void)
{
    uint32_t choice = (gol_rand() >> 16) % 4u;

    gol_clear_grid(current);
    gol_clear_grid(next);
    generation_count = 0;

    switch (choice)
    {
    case 0:
        gol_seed_glider();
        break;
    case 1:
        gol_seed_explorer();
        break;
    case 2:
        gol_seed_ship();
        break;
    default:
        gol_seed_random();
        break;
    }
}

/*
 * Compute the next generation.
 *
 * Live cells age up to LIFE_MAX_AGE.
 * Newly born cells start at age 1.
 */
static void gol_step(void)
{
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            int neighbors = gol_count_neighbors(x, y);
            int alive = gol_is_alive(current[y][x]);

            if (alive)
            {
                if (neighbors == 2 || neighbors == 3)
                {
                    uint8_t age = current[y][x];
                    if (age < MAX_AGE)
                    {
                        age++;
                    }
                    next[y][x] = age;
                }
                else
                {
                    next[y][x] = 0;
                }
            }
            else
            {
                if (neighbors == 3)
                {
                    next[y][x] = 1;
                }
                else
                {
                    next[y][x] = 0;
                }
            }
        }
    }
}

/*
 * Map cell age to a color.
 *
 * Young cells are green, older cells move toward yellow/red.
 */
static led_matrix_color_t gol_color_for_age(uint8_t age)
{
    switch (age)
    {
    case 0:
        return (led_matrix_color_t){0, 0, 0};
    case 1:
        return (led_matrix_color_t){0, 120, 255};
    case 2:
        return (led_matrix_color_t){0, 255, 255};
    case 3:
        return (led_matrix_color_t){0, 255, 120};
    case 4:
        return (led_matrix_color_t){0, 255, 0};
    case 5:
        return (led_matrix_color_t){180, 255, 0};
    case 6:
        return (led_matrix_color_t){255, 180, 0};
    default:
        return (led_matrix_color_t){255, 0, 80};
    }
}

/*
 * Render the current life grid to the LED matrix.
 */
static void gol_render(void)
{
    led_frame_t frame;
    int task_id = gol_get_task_id();

    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            frame.pixels[y][x] = gol_color_for_age(current[y][x]);
        }
    }

    led_submit_frame(task_id, &frame);
}

/*
 * Register the task ID.
 */
void gol_register_task_id(int id)
{
    gol_task_id = id;
}

/*
 * Return the registered task ID, or -1 if none exists.
 */
int gol_get_task_id(void)
{
    return gol_task_id;
}

/*
 * Game of Life demo task for the Sense HAT LED matrix.
 */
void gol_task(void)
{
    int task_id = scheduler_current_task_id();

    gol_register_task_id(task_id);

    console_puts("gol: started\n");

    if (led_acquire(task_id) < 0)
    {
        console_puts("gol: LED matrix already in use\n");
        gol_register_task_id(-1);
        return;
    }

    rng_state ^= (uint32_t)timer_get_ticks();

    gol_seed_pattern();
    gol_render();

    while (1)
    {
        task_sleep(FRAME_DELAY_TICKS);

        gol_step();
        generation_count++;

        if (!gol_any_alive() || gol_grids_equal(current, next) || generation_count >= RESEED_AFTER_FRAMES)
        {
            gol_seed_pattern();
        }
        else
        {
            gol_copy_next_to_current();
        }

        gol_render();
    }

    led_release(task_id);
    gol_register_task_id(-1);
}