#include "kernel/tasks/tictactoe_task.h"
#include "kernel/tasks/joystick_task.h"
#include "kernel/tasks/led_task.h"
#include "kernel/io/console.h"
#include "kernel/io/hdmi_console.h"
#include "kernel/sched/scheduler.h"
#include "kernel/irq.h"
#include "kernel/timer.h"
#include "rpi4/hdmi.h"
#include "util/string.h"

/* Game modes. */
#define TTT_MODE_CPU_EASY 0
#define TTT_MODE_CPU_HARD 1
#define TTT_MODE_PVP 2

/* Cell values. */
#define TTT_CELL_EMPTY 0
#define TTT_CELL_X 1
#define TTT_CELL_O 2

/* Game states. */
#define TTT_STATE_MENU 0
#define TTT_STATE_PLAYING 1
#define TTT_STATE_RESULT 2

/* Result menu items. */
#define TTT_RESULT_PLAY_AGAIN 0
#define TTT_RESULT_MODE_MENU 1
#define TTT_RESULT_EXIT 2

/* HDMI text palette for the Tic-Tac-Toe screen. */
#define HDMI_TTT_BG 0x00161F2Au
#define HDMI_TTT_TITLE 0x0098F4FFu
#define HDMI_TTT_MUTED 0x0087A3B7u
#define HDMI_TTT_X 0x00FF6B6Bu
#define HDMI_TTT_O 0x0041E4FFu
#define HDMI_TTT_WIN 0x00FFCE54u
#define HDMI_TTT_CURSOR 0x0048E27Bu
#define HDMI_TTT_LINE 0x0028C7FAu
#define HDMI_TTT_STATUS 0x00F2F6F8u
#define TTT_HDMI_LINE_WIDTH 44
#define TTT_HDMI_CLEAR_FROM 24
#define TTT_JOY_QUEUE_SIZE 16

/* Sense HAT LED mirror palette. */
#define TTT_LED_LINE_R 255
#define TTT_LED_LINE_G 255
#define TTT_LED_LINE_B 255
#define TTT_LED_X_R 255
#define TTT_LED_X_G 80
#define TTT_LED_X_B 80
#define TTT_LED_O_R 60
#define TTT_LED_O_G 170
#define TTT_LED_O_B 255
#define TTT_LED_WIN_R 255
#define TTT_LED_WIN_G 210
#define TTT_LED_WIN_B 80
#define TTT_LED_CURSOR_R 40
#define TTT_LED_CURSOR_G 200
#define TTT_LED_CURSOR_B 80
#define TTT_LED_MODE_R 80
#define TTT_LED_MODE_G 220
#define TTT_LED_MODE_B 255
#define TTT_LED_EXIT_R 255
#define TTT_LED_EXIT_G 90
#define TTT_LED_EXIT_B 180

/* Game structure. */
typedef struct
{
    int board[9];
    int cursor;
    int mode;
    int state;
    int current_player;
    int winner;
    int moves;
    int menu_index;
    int result_index;
    int win_line;
} ttt_game_t;

static volatile joy_event_t ttt_joy_queue[TTT_JOY_QUEUE_SIZE];
static volatile unsigned int ttt_joy_head = 0;
static volatile unsigned int ttt_joy_tail = 0;
static int ttt_task_id = -1;
static int ttt_led_enabled = 0;
static int ttt_hdmi_enabled = 0;

/* Winning lines. */
static const int ttt_lines[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

static void h(const char *s) { hdmi_puts(s); }
static void hc(uint32_t fg, const char *s)
{
    hdmi_set_text_colors(fg, HDMI_TTT_BG);
    hdmi_puts(s);
    hdmi_reset_text_colors();
}

static void ttt_hdmi_pad_line(int used)
{
    hdmi_set_text_colors(HDMI_TTT_STATUS, HDMI_TTT_BG);
    while (used < TTT_HDMI_LINE_WIDTH)
    {
        hdmi_putc(' ');
        used++;
    }
    hdmi_reset_text_colors();
}

static void ttt_hdmi_write_line(uint32_t row, uint32_t fg, const char *s)
{
    int used = str_length(s);

    hdmi_set_cursor(0u, row);
    hdmi_set_text_colors(fg, HDMI_TTT_BG);
    hdmi_puts(s);
    hdmi_reset_text_colors();
    ttt_hdmi_pad_line(used);
}

static void ttt_hdmi_clear_from(uint32_t row)
{
    while (row < TTT_HDMI_CLEAR_FROM)
    {
        ttt_hdmi_write_line(row, HDMI_TTT_STATUS, "");
        row++;
    }
}

static int ttt_joy_queue_is_empty(void)
{
    return ttt_joy_head == ttt_joy_tail;
}

static void ttt_joystick_enqueue_event(joy_event_t event)
{
    unsigned int next;

    if (event == JOY_EVENT_NONE)
    {
        return;
    }

    irq_disable();
    next = (ttt_joy_head + 1u) % TTT_JOY_QUEUE_SIZE;

    if (next != ttt_joy_tail)
    {
        ttt_joy_queue[ttt_joy_head] = event;
        ttt_joy_head = next;
    }

    irq_enable();

    if (ttt_task_id >= 0)
    {
        task_wakeup(ttt_task_id);
    }
}

static joy_event_t ttt_joystick_read_event(void)
{
    joy_event_t event;

    irq_disable();

    if (ttt_joy_queue_is_empty())
    {
        irq_enable();
        return JOY_EVENT_NONE;
    }

    event = ttt_joy_queue[ttt_joy_tail];
    ttt_joy_tail = (ttt_joy_tail + 1u) % TTT_JOY_QUEUE_SIZE;

    irq_enable();
    return event;
}

static void ttt_joystick_event_handler(joy_event_t event)
{
    ttt_joystick_enqueue_event(event);
}

static int ttt_is_cpu_turn(const ttt_game_t *game);
static int ttt_is_on_win_line(const ttt_game_t *game, int index);

static const char *ttt_mode_label(int mode)
{
    if (mode == TTT_MODE_CPU_EASY)
        return "Easy CPU";
    if (mode == TTT_MODE_CPU_HARD)
        return "Hard CPU";
    return "Two Players";
}

static int ttt_cursor_visible(void)
{
    return ((timer_get_ticks() / 25u) % 2u) == 0u;
}

static led_matrix_color_t ttt_led_color(uint8_t r, uint8_t g, uint8_t b)
{
    led_matrix_color_t color = {r, g, b};
    return color;
}

static void ttt_led_fill_cell(led_frame_t *frame, int cell_index, led_matrix_color_t color)
{
    int row = cell_index / 3;
    int col = cell_index % 3;
    int base_y = row * 3;
    int base_x = col * 3;

    for (int y = 0; y < 2; y++)
    {
        for (int x = 0; x < 2; x++)
        {
            frame->pixels[base_y + y][base_x + x] = color;
        }
    }
}

static led_matrix_color_t ttt_led_scale(led_matrix_color_t color, uint8_t scale)
{
    led_matrix_color_t scaled;

    scaled.r = (uint8_t)(((uint16_t)color.r * scale) / 255u);
    scaled.g = (uint8_t)(((uint16_t)color.g * scale) / 255u);
    scaled.b = (uint8_t)(((uint16_t)color.b * scale) / 255u);

    return scaled;
}

static void ttt_led_mark_cursor(led_frame_t *frame, int cell_index, led_matrix_color_t color)
{
    int row = cell_index / 3;
    int col = cell_index % 3;
    int base_y = row * 3;
    int base_x = col * 3;

    if (base_x > 0)
    {
        frame->pixels[base_y][base_x - 1] = color;
        frame->pixels[base_y + 1][base_x - 1] = color;
    }

    if ((base_x + 2) < MATRIX_WIDTH)
    {
        frame->pixels[base_y][base_x + 2] = color;
        frame->pixels[base_y + 1][base_x + 2] = color;
    }

    if (base_y > 0)
    {
        frame->pixels[base_y - 1][base_x] = color;
        frame->pixels[base_y - 1][base_x + 1] = color;
    }

    if ((base_y + 2) < MATRIX_HEIGHT)
    {
        frame->pixels[base_y + 2][base_x] = color;
        frame->pixels[base_y + 2][base_x + 1] = color;
    }
}

static void ttt_render_led(const ttt_game_t *game)
{
    led_frame_t frame;
    led_matrix_color_t black = ttt_led_color(0, 0, 0);
    led_matrix_color_t line = ttt_led_color(TTT_LED_LINE_R, TTT_LED_LINE_G, TTT_LED_LINE_B);
    led_matrix_color_t cursor = ttt_led_color(TTT_LED_CURSOR_R, TTT_LED_CURSOR_G, TTT_LED_CURSOR_B);
    uint64_t phase = timer_get_ticks() / 6u;
    int cursor_on = ttt_cursor_visible();

    if (!ttt_led_enabled || ttt_task_id < 0)
    {
        return;
    }

    for (int y = 0; y < MATRIX_HEIGHT; y++)
    {
        for (int x = 0; x < MATRIX_WIDTH; x++)
        {
            frame.pixels[y][x] = black;
        }
    }

    for (int i = 0; i < MATRIX_WIDTH; i++)
    {
        frame.pixels[2][i] = line;
        frame.pixels[5][i] = line;
        frame.pixels[i][2] = line;
        frame.pixels[i][5] = line;
    }

    if (game->state == TTT_STATE_RESULT)
    {
        if (game->result_index == TTT_RESULT_PLAY_AGAIN)
        {
            line = (phase % 2u) == 0u ? ttt_led_color(TTT_LED_LINE_R, TTT_LED_LINE_G, TTT_LED_LINE_B)
                                      : ttt_led_color(TTT_LED_WIN_R, TTT_LED_WIN_G, TTT_LED_WIN_B);
        }
        else if (game->result_index == TTT_RESULT_MODE_MENU)
        {
            line = (phase % 2u) == 0u ? ttt_led_color(TTT_LED_MODE_R, TTT_LED_MODE_G, TTT_LED_MODE_B)
                                      : ttt_led_scale(ttt_led_color(TTT_LED_MODE_R, TTT_LED_MODE_G, TTT_LED_MODE_B), 140);
        }
        else
        {
            line = (phase % 2u) == 0u ? ttt_led_color(TTT_LED_EXIT_R, TTT_LED_EXIT_G, TTT_LED_EXIT_B)
                                      : ttt_led_scale(ttt_led_color(TTT_LED_EXIT_R, TTT_LED_EXIT_G, TTT_LED_EXIT_B), 140);
        }

        for (int i = 0; i < MATRIX_WIDTH; i++)
        {
            frame.pixels[2][i] = line;
            frame.pixels[5][i] = line;
            frame.pixels[i][2] = line;
            frame.pixels[i][5] = line;
        }
    }

    if (game->state == TTT_STATE_PLAYING || game->state == TTT_STATE_RESULT)
    {
        for (int index = 0; index < 9; index++)
        {
            int value = game->board[index];
            int is_cursor = (game->cursor == index) && !ttt_is_cpu_turn(game);
            int is_winning = ttt_is_on_win_line(game, index) && game->winner != TTT_CELL_EMPTY;
            led_matrix_color_t color = black;

            if (value == TTT_CELL_X)
            {
                color = is_winning
                            ? ((phase % 2u) == 0u
                                   ? ttt_led_color(TTT_LED_WIN_R, TTT_LED_WIN_G, TTT_LED_WIN_B)
                                   : ttt_led_scale(ttt_led_color(TTT_LED_X_R, TTT_LED_X_G, TTT_LED_X_B), 110))
                            : ttt_led_color(TTT_LED_X_R, TTT_LED_X_G, TTT_LED_X_B);
            }
            else if (value == TTT_CELL_O)
            {
                color = is_winning
                            ? ((phase % 2u) == 0u
                                   ? ttt_led_color(TTT_LED_WIN_R, TTT_LED_WIN_G, TTT_LED_WIN_B)
                                   : ttt_led_scale(ttt_led_color(TTT_LED_O_R, TTT_LED_O_G, TTT_LED_O_B), 110))
                            : ttt_led_color(TTT_LED_O_R, TTT_LED_O_G, TTT_LED_O_B);
            }
            else if (is_cursor && cursor_on)
            {
                color = cursor;
            }

            ttt_led_fill_cell(&frame, index, color);

            if (is_cursor && cursor_on)
            {
                ttt_led_mark_cursor(&frame, index, cursor);
            }
        }
    }

    led_submit_frame(ttt_task_id, &frame);
}

/*
 * Return the winner for the current board, or TTT_CELL_EMPTY if none exists.
 */
static int ttt_check_winner(const int *board)
{
    for (int i = 0; i < 8; i++)
    {
        int a = ttt_lines[i][0];
        int b = ttt_lines[i][1];
        int c = ttt_lines[i][2];
        if (board[a] != TTT_CELL_EMPTY &&
            board[a] == board[b] &&
            board[b] == board[c])
        {
            return board[a];
        }
    }
    return TTT_CELL_EMPTY;
}

/*
 * Return the winning line index, or -1 if no line is complete.
 */
static int ttt_check_winner_line(const int *board)
{
    for (int i = 0; i < 8; i++)
    {
        int a = ttt_lines[i][0];
        int b = ttt_lines[i][1];
        int c = ttt_lines[i][2];
        if (board[a] != TTT_CELL_EMPTY &&
            board[a] == board[b] &&
            board[b] == board[c])
        {
            return i;
        }
    }
    return -1;
}

/*
 * Return non-zero if the given cell belongs to the current winning line.
 */
static int ttt_is_on_win_line(const ttt_game_t *game, int index)
{
    if (game->win_line < 0)
        return 0;
    return (ttt_lines[game->win_line][0] == index ||
            ttt_lines[game->win_line][1] == index ||
            ttt_lines[game->win_line][2] == index);
}

/*
 * Evaluate the board recursively for hard CPU mode.
 */
static int ttt_minimax(int *board, int depth, int is_maximizing, int moves_left)
{
    int winner = ttt_check_winner(board);
    if (winner == TTT_CELL_O)
        return 10 - depth;
    if (winner == TTT_CELL_X)
        return -10 + depth;
    if (moves_left == 0)
        return 0;

    if (is_maximizing)
    {
        int best = -100;
        for (int i = 0; i < 9; i++)
        {
            if (board[i] != TTT_CELL_EMPTY)
                continue;
            board[i] = TTT_CELL_O;
            int score = ttt_minimax(board, depth + 1, 0, moves_left - 1);
            board[i] = TTT_CELL_EMPTY;
            if (score > best)
                best = score;
        }
        return best;
    }
    else
    {
        int best = 100;
        for (int i = 0; i < 9; i++)
        {
            if (board[i] != TTT_CELL_EMPTY)
                continue;
            board[i] = TTT_CELL_X;
            int score = ttt_minimax(board, depth + 1, 1, moves_left - 1);
            board[i] = TTT_CELL_EMPTY;
            if (score < best)
                best = score;
        }
        return best;
    }
}

static int ttt_find_winning_move(const int *board, int player)
{
    int test_board[9];
    for (int i = 0; i < 9; i++)
    {
        if (board[i] != TTT_CELL_EMPTY)
            continue;
        for (int j = 0; j < 9; j++)
            test_board[j] = board[j];
        test_board[i] = player;
        if (ttt_check_winner(test_board) == player)
            return i;
    }
    return -1;
}

/*
 * Pick a move for the easy CPU.
 *
 * The strategy is:
 * - win immediately if possible
 * - block the opponent if needed
 * - otherwise prefer center, corners, then edges
 */
static int ttt_pick_cpu_move_easy(const ttt_game_t *game)
{
    static const int preference[] = {4, 0, 2, 6, 8, 1, 3, 5, 7};
    int move;

    move = ttt_find_winning_move(game->board, TTT_CELL_O);
    if (move >= 0)
        return move;

    move = ttt_find_winning_move(game->board, TTT_CELL_X);
    if (move >= 0)
        return move;

    for (unsigned int i = 0; i < sizeof(preference) / sizeof(preference[0]); i++)
        if (game->board[preference[i]] == TTT_CELL_EMPTY)
            return preference[i];

    return -1;
}

/*
 * Pick a move for the hard CPU using minimax.
 */
static int ttt_pick_cpu_move_hard(const ttt_game_t *game)
{
    int board_copy[9];
    int best_score = -100;
    int best_move = -1;
    int empty_count = 0;

    for (int i = 0; i < 9; i++)
    {
        board_copy[i] = game->board[i];
        if (game->board[i] == TTT_CELL_EMPTY)
            empty_count++;
    }

    for (int i = 0; i < 9; i++)
    {
        if (board_copy[i] != TTT_CELL_EMPTY)
            continue;
        board_copy[i] = TTT_CELL_O;
        int score = ttt_minimax(board_copy, 0, 0, empty_count - 1);
        board_copy[i] = TTT_CELL_EMPTY;
        if (score > best_score)
        {
            best_score = score;
            best_move = i;
        }
    }
    return best_move;
}

/*
 * Reset the board and initialize one fresh round.
 */
static void ttt_reset_board(ttt_game_t *game)
{
    for (int i = 0; i < 9; i++)
        game->board[i] = TTT_CELL_EMPTY;
    game->cursor = 4;
    game->current_player = TTT_CELL_X;
    game->winner = TTT_CELL_EMPTY;
    game->moves = 0;
    game->result_index = 0;
    game->win_line = -1;
    game->state = TTT_STATE_PLAYING;
}

/*
 * Move the game into the result state when a win or draw is reached.
 */
static void ttt_finish_if_needed(ttt_game_t *game)
{
    int winner = ttt_check_winner(game->board);
    if (winner != TTT_CELL_EMPTY)
    {
        game->winner = winner;
        game->win_line = ttt_check_winner_line(game->board);
        game->state = TTT_STATE_RESULT;
        return;
    }
    if (game->moves >= 9)
    {
        game->winner = TTT_CELL_EMPTY;
        game->win_line = -1;
        game->state = TTT_STATE_RESULT;
    }
}

/*
 * Place the current player's mark into the selected cell.
 */
static void ttt_apply_move(ttt_game_t *game, int index)
{
    if (index < 0 || index >= 9)
        return;
    if (game->board[index] != TTT_CELL_EMPTY || game->state != TTT_STATE_PLAYING)
        return;

    game->board[index] = game->current_player;
    game->cursor = index;
    game->moves++;
    ttt_finish_if_needed(game);

    if (game->state == TTT_STATE_PLAYING)
        game->current_player = (game->current_player == TTT_CELL_X) ? TTT_CELL_O : TTT_CELL_X;
}

/*
 * Return non-zero if the CPU should make the next move.
 */
static int ttt_is_cpu_turn(const ttt_game_t *game)
{
    return game->state == TTT_STATE_PLAYING &&
           game->mode != TTT_MODE_PVP &&
           game->current_player == TTT_CELL_O;
}

/*
 * Run one CPU turn.
 */
static void ttt_run_cpu_turn(ttt_game_t *game)
{
    if (!ttt_is_cpu_turn(game))
        return;
    int move = (game->mode == TTT_MODE_CPU_HARD)
                   ? ttt_pick_cpu_move_hard(game)
                   : ttt_pick_cpu_move_easy(game);
    if (move >= 0)
        ttt_apply_move(game, move);
}

/*
 * Move the board cursor with W/A/S/D and wrap around the edges.
 */
static void ttt_move_cursor(ttt_game_t *game, char input)
{
    int row = game->cursor / 3;
    int col = game->cursor % 3;

    if (input == 'w' || input == 'W')
        row = (row + 2) % 3;
    else if (input == 's' || input == 'S')
        row = (row + 1) % 3;
    else if (input == 'a' || input == 'A')
        col = (col + 2) % 3;
    else if (input == 'd' || input == 'D')
        col = (col + 1) % 3;

    game->cursor = row * 3 + col;
}

static void ttt_render_cell_hdmi(const ttt_game_t *game, int index)
{
    int value = game->board[index];
    int is_cursor = (game->cursor == index) && !ttt_is_cpu_turn(game) && ttt_cursor_visible();
    int is_winning = ttt_is_on_win_line(game, index) && game->winner != TTT_CELL_EMPTY;

    if (is_cursor)
        hc(HDMI_TTT_CURSOR, "[");
    else if (is_winning)
        hc(HDMI_TTT_WIN, "*");
    else
        h(" ");

    if (value == TTT_CELL_X)
    {
        hc(is_winning ? HDMI_TTT_WIN : HDMI_TTT_X, "X");
    }
    else if (value == TTT_CELL_O)
    {
        hc(is_winning ? HDMI_TTT_WIN : HDMI_TTT_O, "O");
    }
    else
    {
        h(" ");
    }

    if (is_cursor)
        hc(HDMI_TTT_CURSOR, "]");
    else if (is_winning)
        hc(HDMI_TTT_WIN, "*");
    else
        h(" ");
}

static void ttt_render_hdmi_header(const ttt_game_t *game)
{
    ttt_hdmi_write_line(0u, HDMI_TTT_TITLE, "TIC TAC TOE");
    ttt_hdmi_write_line(1u, HDMI_TTT_LINE, "===========");
    ttt_hdmi_write_line(2u, HDMI_TTT_MUTED, "");
    ttt_hdmi_write_line(3u, HDMI_TTT_MUTED, ttt_mode_label(game->mode));

    if (game->state == TTT_STATE_PLAYING)
    {
        if (ttt_is_cpu_turn(game))
        {
            static const char *frames[] = {
                "Status: CPU is thinking   ",
                "Status: CPU is thinking.  ",
                "Status: CPU is thinking.. ",
                "Status: CPU is thinking..."};
            uint64_t phase = (timer_get_ticks() / 8u) % 4u;
            ttt_hdmi_write_line(4u, HDMI_TTT_O, "Turn: CPU");
            ttt_hdmi_write_line(5u, HDMI_TTT_STATUS, frames[phase]);
        }
        else
        {
            ttt_hdmi_write_line(4u, game->current_player == TTT_CELL_X ? HDMI_TTT_X : HDMI_TTT_O,
                                game->current_player == TTT_CELL_X ? "Turn: X" : "Turn: O");
            ttt_hdmi_write_line(5u, HDMI_TTT_STATUS, "Status: Your move");
        }
    }
    else if (game->state == TTT_STATE_RESULT)
    {
        if (game->winner == TTT_CELL_X)
        {
            ttt_hdmi_write_line(4u, HDMI_TTT_WIN, "Result: X wins");
        }
        else if (game->winner == TTT_CELL_O)
        {
            ttt_hdmi_write_line(4u, HDMI_TTT_WIN, game->mode == TTT_MODE_PVP ? "Result: O wins" : "Result: CPU wins");
        }
        else
        {
            ttt_hdmi_write_line(4u, HDMI_TTT_WIN, "Result: Draw");
        }

        ttt_hdmi_write_line(5u, HDMI_TTT_MUTED, "");
    }
}

/*
 * Render the current board state to HDMI.
 */
static void ttt_render_board_hdmi(const ttt_game_t *game)
{
    int used;

    ttt_render_hdmi_header(game);
    ttt_hdmi_write_line(6u, HDMI_TTT_MUTED, "");
    ttt_hdmi_write_line(7u, HDMI_TTT_LINE, "+---+---+---+");

    for (int row = 0; row < 3; row++)
    {
        hdmi_set_cursor(0u, 8u + (uint32_t)(row * 2));
        used = 0;
        hc(HDMI_TTT_LINE, "|");
        used++;
        for (int col = 0; col < 3; col++)
        {
            int index = row * 3 + col;
            ttt_render_cell_hdmi(game, index);
            used += 3;
            hc(HDMI_TTT_LINE, "|");
            used++;
        }
        ttt_hdmi_pad_line(used);
        ttt_hdmi_write_line(9u + (uint32_t)(row * 2), HDMI_TTT_LINE, "+---+---+---+");
    }

    ttt_hdmi_write_line(14u, HDMI_TTT_MUTED, "[ ] cursor   * * winning line");
    ttt_hdmi_write_line(15u, HDMI_TTT_MUTED, "Move with joystick, press center");
    ttt_hdmi_clear_from(16u);
}

static void ttt_render_menu_hdmi(const ttt_game_t *game)
{
    const char *modes[3] = {"Easy CPU", "Hard CPU", "Two Players"};

    ttt_hdmi_write_line(0u, HDMI_TTT_TITLE, "TIC TAC TOE");
    ttt_hdmi_write_line(1u, HDMI_TTT_LINE, "===========");
    ttt_hdmi_write_line(2u, HDMI_TTT_MUTED, "");
    ttt_hdmi_write_line(3u, HDMI_TTT_STATUS, "Choose a mode");
    ttt_hdmi_write_line(4u, HDMI_TTT_LINE, "-------------");

    for (int i = 0; i < 3; i++)
    {
        hdmi_set_cursor(0u, 6u + (uint32_t)i);
        if (game->menu_index == i)
            hc(HDMI_TTT_CURSOR, "> ");
        else
            h("  ");
        hc(game->menu_index == i ? HDMI_TTT_STATUS : HDMI_TTT_MUTED, modes[i]);
        ttt_hdmi_pad_line(2 + str_length(modes[i]));
    }

    ttt_hdmi_write_line(10u, HDMI_TTT_MUTED, "");
    ttt_hdmi_write_line(11u, HDMI_TTT_MUTED, "X starts every round");
    ttt_hdmi_write_line(12u, HDMI_TTT_MUTED, "Use UP/DOWN, then CENTER");
    ttt_hdmi_write_line(13u, HDMI_TTT_MUTED, "During the game: directions and CENTER");
    ttt_hdmi_clear_from(14u);
}

static void ttt_render_result_hdmi(const ttt_game_t *game)
{
    const char *options[3] = {"Play again", "Change mode", "Exit game"};

    ttt_render_board_hdmi(game);
    ttt_hdmi_write_line(16u, HDMI_TTT_MUTED, "");
    ttt_hdmi_write_line(17u, HDMI_TTT_STATUS, "Next step");
    ttt_hdmi_write_line(18u, HDMI_TTT_LINE, "---------");

    for (int i = 0; i < 3; i++)
    {
        hdmi_set_cursor(0u, 19u + (uint32_t)i);
        if (game->result_index == i)
        {
            hc(HDMI_TTT_CURSOR, "> ");
            hc(HDMI_TTT_STATUS, options[i]);
        }
        else
        {
            h("  ");
            hc(HDMI_TTT_MUTED, options[i]);
        }

        ttt_hdmi_pad_line(2 + str_length(options[i]));
    }

    ttt_hdmi_clear_from(22u);
}

/*
 * Render the current game screen to HDMI without clearing the whole console.
 */
static void ttt_render_hdmi(const ttt_game_t *game, int clear)
{
    if (!ttt_hdmi_enabled)
    {
        return;
    }

    if (clear)
    {
        hdmi_clear_console();
    }

    if (game->state == TTT_STATE_MENU)
    {
        ttt_render_menu_hdmi(game);
    }
    else if (game->state == TTT_STATE_PLAYING)
    {
        ttt_render_board_hdmi(game);
    }
    else
    {
        ttt_render_result_hdmi(game);
    }

    while (hdmi_flush_dirty(32u)){

    }
}

static void ttt_render_outputs(const ttt_game_t *game, int clear_hdmi)
{
    if (ttt_hdmi_enabled)
    {
        ttt_render_hdmi(game, clear_hdmi);
    }

    if (ttt_led_enabled)
    {
        ttt_render_led(game);
    }
}

static int ttt_handle_menu_joy(ttt_game_t *game, joy_event_t event)
{
    if (event == JOY_EVENT_UP)
    {
        game->menu_index = (game->menu_index + 2) % 3;
    }
    else if (event == JOY_EVENT_DOWN)
    {
        game->menu_index = (game->menu_index + 1) % 3;
    }
    else if (event == JOY_EVENT_CENTER_RELEASE)
    {
        if (game->menu_index == 0)
            game->mode = TTT_MODE_CPU_EASY;
        else if (game->menu_index == 1)
            game->mode = TTT_MODE_CPU_HARD;
        else
            game->mode = TTT_MODE_PVP;
        ttt_reset_board(game);
    }

    return 1;
}

static int ttt_handle_result_joy(ttt_game_t *game, joy_event_t event)
{
    if (event == JOY_EVENT_UP)
    {
        game->result_index = (game->result_index + 2) % 3;
    }
    else if (event == JOY_EVENT_DOWN)
    {
        game->result_index = (game->result_index + 1) % 3;
    }
    else if (event == JOY_EVENT_CENTER_RELEASE)
    {
        if (game->result_index == TTT_RESULT_PLAY_AGAIN)
        {
            ttt_reset_board(game);
        }
        else if (game->result_index == TTT_RESULT_MODE_MENU)
        {
            game->state = TTT_STATE_MENU;
            game->menu_index = 0;
        }
        else
        {
            return 0;
        }
    }

    return 1;
}

static void ttt_handle_play_joy(ttt_game_t *game, joy_event_t event)
{
    if (ttt_is_cpu_turn(game))
    {
        return;
    }

    if (event == JOY_EVENT_UP)
    {
        ttt_move_cursor(game, 'w');
    }
    else if (event == JOY_EVENT_DOWN)
    {
        ttt_move_cursor(game, 's');
    }
    else if (event == JOY_EVENT_LEFT)
    {
        ttt_move_cursor(game, 'a');
    }
    else if (event == JOY_EVENT_RIGHT)
    {
        ttt_move_cursor(game, 'd');
    }
    else if (event == JOY_EVENT_CENTER_RELEASE)
    {
        ttt_apply_move(game, game->cursor);
    }
}

/*
 * Tic-Tac-Toe task entry point.
 *
 * The game uses the joystick for input and renders to the LED matrix
 * and/or HDMI if those outputs are available. It does not take over
 * UART input, so the shell remains usable while the game is running.
 */
void tictactoe_task(void)
{
    ttt_game_t game;
    int running = 1;
    int display_dirty = 1;
    int last_state = -1;
    int last_cursor_blink = -1;
    int last_cpu_phase = -1;

    game.mode = TTT_MODE_CPU_EASY;
    game.state = TTT_STATE_MENU;
    game.menu_index = 0;
    game.result_index = 0;
    game.cursor = 4;
    game.current_player = TTT_CELL_X;
    game.winner = TTT_CELL_EMPTY;
    game.moves = 0;
    game.win_line = -1;
    for (int i = 0; i < 9; i++)
        game.board[i] = TTT_CELL_EMPTY;

    ttt_task_id = scheduler_current_task_id();
    ttt_joy_head = 0;
    ttt_joy_tail = 0;
    ttt_led_enabled = 0;
    ttt_hdmi_enabled = 0;

    if (led_acquire(ttt_task_id) == 0)
    {
        ttt_led_enabled = 1;
    }

    if (hdmi_acquire(ttt_task_id) == 0)
    {
        ttt_hdmi_enabled = 1;
        hdmi_console_enable(0);
        hdmi_clear_console();
    }

    if (!ttt_led_enabled && !ttt_hdmi_enabled)
    {
        console_puts("tictactoe: no display available\n");
        ttt_task_id = -1;
        return;
    }

    if (joystick_set_event_handler(ttt_joystick_event_handler) < 0)
    {
        console_puts("tictactoe: joystick not available\n");

        if (ttt_led_enabled)
        {
            led_release(ttt_task_id);
        }

        if (ttt_hdmi_enabled)
        {
            hdmi_clear_console();
            hdmi_release(ttt_task_id);
            hdmi_console_enable(1);
        }

        ttt_led_enabled = 0;
        ttt_hdmi_enabled = 0;
        ttt_task_id = -1;
        return;
    }

    while (running)
    {
        joy_event_t joy;
        int clear_hdmi = 0;
        int cursor_blink = ttt_cursor_visible();
        int cpu_phase = (int)((timer_get_ticks() / 8u) % 4u);

        if (game.state != last_state)
        {
            clear_hdmi = 1;
            display_dirty = 1;
            last_state = game.state;
        }

        if (ttt_is_cpu_turn(&game))
        {
            if (cpu_phase != last_cpu_phase)
            {
                display_dirty = 1;
                last_cpu_phase = cpu_phase;
            }

            if (display_dirty)
            {
                ttt_render_outputs(&game, clear_hdmi);
                display_dirty = 0;
            }

            task_sleep(20);
            ttt_run_cpu_turn(&game);
            display_dirty = 1;
            continue;
        }

        last_cpu_phase = -1;

        if (cursor_blink != last_cursor_blink && game.state == TTT_STATE_PLAYING)
        {
            display_dirty = 1;
            last_cursor_blink = cursor_blink;
        }

        if (display_dirty)
        {
            ttt_render_outputs(&game, clear_hdmi);
            display_dirty = 0;
        }

        joy = ttt_joystick_read_event();
        if (joy != JOY_EVENT_NONE)
        {
            if (game.state == TTT_STATE_MENU)
            {
                running = ttt_handle_menu_joy(&game, joy);
            }
            else if (game.state == TTT_STATE_RESULT)
            {
                running = ttt_handle_result_joy(&game, joy);
            }
            else
            {
                ttt_handle_play_joy(&game, joy);
            }

            display_dirty = 1;
            continue;
        }

        task_sleep(1);
    }

    joystick_clear_event_handler();
    if (ttt_hdmi_enabled)
    {
        hdmi_clear_console();
        hdmi_release(ttt_task_id);
        hdmi_console_enable(1);
    }

    if (ttt_led_enabled)
    {
        led_release(ttt_task_id);
    }

    ttt_led_enabled = 0;
    ttt_hdmi_enabled = 0;
    ttt_task_id = -1;
}
