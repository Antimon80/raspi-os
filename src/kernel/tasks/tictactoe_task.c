#include "kernel/tasks/tictactoe_task.h"
#include "kernel/shell/shell.h"
#include "kernel/sched/scheduler.h"
#include "rpi4/uart.h"
#include "rpi4/hdmi.h"

/* Game modes. */
#define TTT_MODE_CPU_EASY   0
#define TTT_MODE_CPU_HARD   1
#define TTT_MODE_PVP        2

/* Cell values. */
#define TTT_CELL_EMPTY  0
#define TTT_CELL_X      1
#define TTT_CELL_O      2

/* Game states. */
#define TTT_STATE_MENU      0
#define TTT_STATE_PLAYING   1
#define TTT_STATE_RESULT    2

/* Result menu items. */
#define TTT_RESULT_PLAY_AGAIN   0
#define TTT_RESULT_MODE_MENU    1
#define TTT_RESULT_EXIT         2

/* ANSI colors and styles. */
#define ANSI_RESET      "\x1b[0m"
#define ANSI_BOLD       "\x1b[1m"

#define ANSI_FG_X       "\x1b[1;38;5;196m"
#define ANSI_FG_O       "\x1b[1;38;5;33m"
#define ANSI_FG_WIN     "\x1b[1;38;5;226m"
#define ANSI_FG_DRAW    "\x1b[38;5;244m"
#define ANSI_FG_CPU     "\x1b[38;5;213m"
#define ANSI_FG_GREEN   "\x1b[38;5;46m"
#define ANSI_FG_GRID    "\x1b[38;5;240m"
#define ANSI_FG_TITLE   "\x1b[1;38;5;255m"
#define ANSI_FG_HINT    "\x1b[38;5;242m"

#define ANSI_BG_CURSOR  "\x1b[48;5;237m"
#define ANSI_BG_NORMAL  "\x1b[48;5;234m"

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

/* Winning lines. */
static const int ttt_lines[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
    {0, 4, 8}, {2, 4, 6}
};

static void u(const char *s)  { uart_puts_raw(s); }
static void h(const char *s)  { hdmi_puts(s); }

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
    if (game->win_line < 0) return 0;
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
    if (winner == TTT_CELL_O) return  10 - depth;
    if (winner == TTT_CELL_X) return -10 + depth;
    if (moves_left == 0)      return 0;

    if (is_maximizing)
    {
        int best = -100;
        for (int i = 0; i < 9; i++)
        {
            if (board[i] != TTT_CELL_EMPTY) continue;
            board[i] = TTT_CELL_O;
            int score = ttt_minimax(board, depth + 1, 0, moves_left - 1);
            board[i] = TTT_CELL_EMPTY;
            if (score > best) best = score;
        }
        return best;
    }
    else
    {
        int best = 100;
        for (int i = 0; i < 9; i++)
        {
            if (board[i] != TTT_CELL_EMPTY) continue;
            board[i] = TTT_CELL_X;
            int score = ttt_minimax(board, depth + 1, 1, moves_left - 1);
            board[i] = TTT_CELL_EMPTY;
            if (score < best) best = score;
        }
        return best;
    }
}

static int ttt_find_winning_move(const int *board, int player)
{
    int test_board[9];
    for (int i = 0; i < 9; i++)
    {
        if (board[i] != TTT_CELL_EMPTY) continue;
        for (int j = 0; j < 9; j++) test_board[j] = board[j];
        test_board[i] = player;
        if (ttt_check_winner(test_board) == player) return i;
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
    if (move >= 0) return move;

    move = ttt_find_winning_move(game->board, TTT_CELL_X);
    if (move >= 0) return move;

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
    int best_move  = -1;
    int empty_count = 0;

    for (int i = 0; i < 9; i++)
    {
        board_copy[i] = game->board[i];
        if (game->board[i] == TTT_CELL_EMPTY) empty_count++;
    }

    for (int i = 0; i < 9; i++)
    {
        if (board_copy[i] != TTT_CELL_EMPTY) continue;
        board_copy[i] = TTT_CELL_O;
        int score = ttt_minimax(board_copy, 0, 0, empty_count - 1);
        board_copy[i] = TTT_CELL_EMPTY;
        if (score > best_score)
        {
            best_score = score;
            best_move  = i;
        }
    }
    return best_move;
}

/*
 * Reset the board and initialize one fresh round.
 */
static void ttt_reset_board(ttt_game_t *game)
{
    for (int i = 0; i < 9; i++) game->board[i] = TTT_CELL_EMPTY;
    game->cursor         = 4;
    game->current_player = TTT_CELL_X;
    game->winner         = TTT_CELL_EMPTY;
    game->moves          = 0;
    game->result_index   = 0;
    game->win_line       = -1;
    game->state          = TTT_STATE_PLAYING;
}

/*
 * Move the game into the result state when a win or draw is reached.
 */
static void ttt_finish_if_needed(ttt_game_t *game)
{
    int winner = ttt_check_winner(game->board);
    if (winner != TTT_CELL_EMPTY)
    {
        game->winner   = winner;
        game->win_line = ttt_check_winner_line(game->board);
        game->state    = TTT_STATE_RESULT;
        return;
    }
    if (game->moves >= 9)
    {
        game->winner   = TTT_CELL_EMPTY;
        game->win_line = -1;
        game->state    = TTT_STATE_RESULT;
    }
}

/*
 * Place the current player's mark into the selected cell.
 */
static void ttt_apply_move(ttt_game_t *game, int index)
{
    if (index < 0 || index >= 9) return;
    if (game->board[index] != TTT_CELL_EMPTY || game->state != TTT_STATE_PLAYING) return;

    game->board[index] = game->current_player;
    game->cursor       = index;
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
    return game->state    == TTT_STATE_PLAYING &&
           game->mode     != TTT_MODE_PVP      &&
           game->current_player == TTT_CELL_O;
}

/*
 * Run one CPU turn after a short delay.
 */
static void ttt_run_cpu_turn(ttt_game_t *game)
{
    if (!ttt_is_cpu_turn(game)) return;
    task_sleep(20);
    int move = (game->mode == TTT_MODE_CPU_HARD)
               ? ttt_pick_cpu_move_hard(game)
               : ttt_pick_cpu_move_easy(game);
    if (move >= 0) ttt_apply_move(game, move);
}

/*
 * Move the board cursor with W/A/S/D and wrap around the edges.
 */
static void ttt_move_cursor(ttt_game_t *game, char input)
{
    int row = game->cursor / 3;
    int col = game->cursor % 3;

    if      (input == 'w' || input == 'W') row = (row + 2) % 3;
    else if (input == 's' || input == 'S') row = (row + 1) % 3;
    else if (input == 'a' || input == 'A') col = (col + 2) % 3;
    else if (input == 'd' || input == 'D') col = (col + 1) % 3;

    game->cursor = row * 3 + col;
}

/*
 * Write a single colored player token to UART.
 */
static void u_player(int player)
{
    if (player == TTT_CELL_X)
    {
        u(ANSI_FG_X);
        u(ANSI_BOLD "X");
    }
    else
    {
        u(ANSI_FG_O);
        u(ANSI_BOLD "O");
    }
    u(ANSI_RESET);
}

/*
 * Render one board cell to UART.
 *
 * The current cursor position is highlighted with a background color.
 * Winning cells are rendered in gold after the game ends.
 */
static void ttt_render_cell_uart(const ttt_game_t *game, int index)
{
    int value = game->board[index];
    int is_cursor = (game->cursor == index) && !ttt_is_cpu_turn(game);
    int is_winning = ttt_is_on_win_line(game, index) && game->winner != TTT_CELL_EMPTY;

    if (is_cursor)
    {
        u(ANSI_FG_GREEN ANSI_BOLD "[");
    }
    else
    {
        u(" ");
    }

    if (value == TTT_CELL_X)
    {
        u(is_winning ? ANSI_FG_WIN ANSI_BOLD "X" : ANSI_FG_X ANSI_BOLD "X");
    }
    else if (value == TTT_CELL_O)
    {
        u(is_winning ? ANSI_FG_WIN ANSI_BOLD "O" : ANSI_FG_O ANSI_BOLD "O");
    }
    else
    {
        u(" ");
    }

    if (is_cursor)
    {
        u(ANSI_FG_GREEN ANSI_BOLD "]");
    }
    else
    {
        u(" ");
    }
    u(ANSI_RESET);
}

/*
 * Render one board cell to HDMI.
 *
 * Each cell always occupies exactly three characters so the board layout
 * remains stable while the cursor moves.
 */
static void ttt_render_cell_hdmi(const ttt_game_t *game, int index)
{
    int value = game->board[index];
    int is_cursor = (game->cursor == index) && !ttt_is_cpu_turn(game);

    if (is_cursor) h("[");
    else           h(" ");

    if (value == TTT_CELL_X) h("X");
    else if (value == TTT_CELL_O) h("O");
    else                         h(" ");

    if (is_cursor) h("]");
    else           h(" ");
}

/*
 * Render the current board state to UART.
 */
static void ttt_render_board_uart(const ttt_game_t *game)
{
    u("\n" ANSI_FG_TITLE "TIC TAC TOE\n" ANSI_RESET "\n");

    if (ttt_is_cpu_turn(game))
    {
        u(ANSI_FG_CPU "CPU is thinking...\n" ANSI_RESET);
    }
    else if (game->state == TTT_STATE_PLAYING)
    {
        u("Turn: ");
        u_player(game->current_player);
        u("\n");
    }
    u("\n");

    for (int row = 0; row < 3; row++)
    {
        for (int col = 0; col < 3; col++)
        {
            int index = row * 3 + col;
            ttt_render_cell_uart(game, index);

            if (col < 2)
            {
                u(ANSI_FG_GRID "|" ANSI_RESET);
            }
        }
        u("\n");

        if (row < 2)
        {
            u(ANSI_FG_GRID "---+---+---\n" ANSI_RESET);
        }
    }
}

/*
 * Render the current board state to HDMI.
 */
static void ttt_render_board_hdmi(const ttt_game_t *game)
{
    h("TIC TAC TOE\n\n");

    if (ttt_is_cpu_turn(game))
    {
        h("CPU is thinking...\n");
    }
    else if (game->state == TTT_STATE_PLAYING)
    {
        if (game->current_player == TTT_CELL_X) h("Turn: X\n");
        else                                    h("Turn: O\n");
    }

    h("\n");

    for (int row = 0; row < 3; row++)
    {
        for (int col = 0; col < 3; col++)
        {
            int index = row * 3 + col;
            ttt_render_cell_hdmi(game, index);

            if (col < 2) h(" | ");
        }
        h("\n");

        if (row < 2) h("---+---+---\n");
    }
}

/*
 * Render the mode selection menu.
 */
static void ttt_render_menu(const ttt_game_t *game)
{
    const char *modes[3] = {"Easy CPU", "Hard CPU", "Two Players"};

    u("\n" ANSI_FG_TITLE "TIC TAC TOE\n" ANSI_RESET "\n");
    u(ANSI_FG_HINT "Choose mode with W/S and confirm with SPACE.\n\n" ANSI_RESET);

    for (int i = 0; i < 3; i++)
    {
        if (game->menu_index == i)
        {
            u(ANSI_FG_GREEN ANSI_BOLD);
            u("  > ");
            u(modes[i]);
            u(ANSI_RESET "\n");
        }
        else
        {
            u(ANSI_FG_HINT);
            u("    ");
            u(modes[i]);
            u(ANSI_RESET "\n");
        }
    }

    u("\n" ANSI_FG_HINT "Controls: W A S D move, SPACE places a mark.\nRed = X   Blue = O\n" ANSI_RESET);

    h("TIC TAC TOE\n\n");
    h("Mode:\n");
    for (int i = 0; i < 3; i++)
    {
        if (game->menu_index == i) h("> ");
        else                       h("  ");
        h(modes[i]); h("\n");
    }
    h("\nW/S + SPACE\n");
}

/*
 * Render the end-of-round screen and its action menu.
 */
static void ttt_render_result(const ttt_game_t *game)
{
    ttt_render_board_uart(game);
    ttt_render_board_hdmi(game);

    u("\n");

    if (game->winner == TTT_CELL_X)
    {
        u(ANSI_FG_WIN ANSI_BOLD "X wins!\n" ANSI_RESET);
        h("X wins!\n");
    }
    else if (game->winner == TTT_CELL_O)
    {
        if (game->mode != TTT_MODE_PVP)
        {
            u(ANSI_FG_CPU ANSI_BOLD "CPU wins!\n" ANSI_RESET);
            h("CPU wins!\n");
        }
        else
        {
            u(ANSI_FG_O ANSI_BOLD "O wins!\n" ANSI_RESET);
            h("O wins!\n");
        }
    }
    else
    {
        u(ANSI_FG_DRAW ANSI_BOLD "Draw.\n" ANSI_RESET);
        h("Draw.\n");
    }

    u("\n" ANSI_FG_HINT "Choose with W/S and confirm with SPACE.\n\n" ANSI_RESET);

    const char *options[3] = {"Play again", "Change mode", "Back to shell"};
    for (int i = 0; i < 3; i++)
    {
        if (game->result_index == i)
        {
            u(ANSI_FG_GREEN ANSI_BOLD);
            u("  > "); u(options[i]); u("\n");
            u(ANSI_RESET);
            h("> "); h(options[i]); h("\n");
        }
        else
        {
            u(ANSI_FG_HINT);
            u("    "); u(options[i]); u("\n");
            u(ANSI_RESET);
            h("  "); h(options[i]); h("\n");
        }
    }
}

/*
 * Render the current game screen to UART and HDMI.
 */
static void ttt_render(const ttt_game_t *game)
{
    u("\x1b[2J\x1b[H");
    hdmi_clear_console();

    if (game->state == TTT_STATE_MENU)
    {
        ttt_render_menu(game);
    }
    else if (game->state == TTT_STATE_PLAYING)
    {
        ttt_render_board_uart(game);
        ttt_render_board_hdmi(game);
        u("\n" ANSI_FG_HINT "W A S D move    SPACE place\nGreen brackets mark the current field.\n" ANSI_RESET);
    }
    else
    {
        ttt_render_result(game);
    }
}

/*
 * Handle input while the mode menu is active.
 */
static int ttt_handle_menu_input(ttt_game_t *game, char c)
{
    if (c == 'w' || c == 'W')
        game->menu_index = (game->menu_index + 2) % 3;
    else if (c == 's' || c == 'S')
        game->menu_index = (game->menu_index + 1) % 3;
    else if (c == ' ')
    {
        if      (game->menu_index == 0) game->mode = TTT_MODE_CPU_EASY;
        else if (game->menu_index == 1) game->mode = TTT_MODE_CPU_HARD;
        else                             game->mode = TTT_MODE_PVP;
        ttt_reset_board(game);
    }
    return 1;
}

/*
 * Handle input while the result menu is active.
 */
static int ttt_handle_result_input(ttt_game_t *game, char c)
{
    if      (c == 'w' || c == 'W') game->result_index = (game->result_index + 2) % 3;
    else if (c == 's' || c == 'S') game->result_index = (game->result_index + 1) % 3;
    else if (c == ' ')
    {
        if (game->result_index == TTT_RESULT_PLAY_AGAIN)
            ttt_reset_board(game);
        else if (game->result_index == TTT_RESULT_MODE_MENU)
        {
            game->state      = TTT_STATE_MENU;
            game->menu_index = 0;
        }
        else
            return 0;
    }
    return 1;
}

/*
 * Handle input while a round is in progress.
 */
static void ttt_handle_play_input(ttt_game_t *game, char c)
{
    if (ttt_is_cpu_turn(game)) return;

    if (c == 'w' || c == 'W' || c == 'a' || c == 'A' ||
        c == 's' || c == 'S' || c == 'd' || c == 'D')
        ttt_move_cursor(game, c);
    else if (c == ' ')
        ttt_apply_move(game, game->cursor);
}

/*
 * Tic-Tac-Toe task entry point.
 *
 * This task temporarily takes ownership of UART input, runs the game loop,
 * and restores shell input handling when it exits.
 */
void tictactoe_task(void)
{
    ttt_game_t game;
    int previous_rx_task = uart_get_rx_task();
    int shell_id         = shell_find_task_by_name("shell");
    int running          = 1;

    game.mode           = TTT_MODE_CPU_EASY;
    game.state          = TTT_STATE_MENU;
    game.menu_index     = 0;
    game.result_index   = 0;
    game.cursor         = 4;
    game.current_player = TTT_CELL_X;
    game.winner         = TTT_CELL_EMPTY;
    game.moves          = 0;
    game.win_line       = -1;
    for (int i = 0; i < 9; i++) game.board[i] = TTT_CELL_EMPTY;

    uart_flush_rx();
    uart_set_rx_task(shell_find_task_by_name("tictactoe"));

    while (running)
    {
        char c;

        ttt_render(&game);

        if (ttt_is_cpu_turn(&game))
        {
            ttt_run_cpu_turn(&game);
            continue;
        }

        if (!uart_read_char_blocking(&c))
            break;

        if      (game.state == TTT_STATE_MENU)    running = ttt_handle_menu_input(&game, c);
        else if (game.state == TTT_STATE_RESULT)  running = ttt_handle_result_input(&game, c);
        else                                       ttt_handle_play_input(&game, c);
    }

    u(ANSI_RESET "\x1b[2J\x1b[H");
    if (shell_id >= 0) uart_set_rx_task(shell_id);
    else               uart_set_rx_task(previous_rx_task);

    hdmi_clear_console();
    uart_puts("Back in shell.\n> ");
}
