#include "commands.h"

#include <string.h>

/* Soft key codes returned by read_key() for terminal escape sequences.
 * They live above the ASCII range so they never collide with a real byte.
 * Keep in sync with rvi.c. */
#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_RIGHT 1002
#define KEY_LEFT  1003
#define KEY_HOME  1004
#define KEY_END   1005
#define KEY_DELETE 1006
#define KEY_PGUP  1007
#define KEY_PGDN  1008

static void clear_pending(Editor *e)
{
    e->pending = PENDING_NONE;
}

static void handle_delete_command(Editor *e, int key)
{
    clear_pending(e);

    switch (key) {
    case 'd':
        editor_delete_line(e);
        break;

    case 'w':
        editor_delete_word(e);
        break;

    case '$':
        editor_delete_to_end(e);
        break;

    default:
        editor_set_status(e, "Unknown delete command");
        break;
    }
}

static void handle_yank_command(Editor *e, int key)
{
    clear_pending(e);

    switch (key) {
    case 'y':
        editor_yank_line(e);
        break;

    case 'w':
        editor_yank_word(e);
        break;

    case '$':
        editor_yank_to_end(e);
        break;

    default:
        editor_set_status(e, "Unknown yank command");
        break;
    }
}

void command_handle_normal(Editor *e, int key)
{
    if (e->pending == PENDING_DELETE) {
        handle_delete_command(e, key);
        return;
    }

    if (e->pending == PENDING_YANK) {
        handle_yank_command(e, key);
        return;
    }

    switch (key) {
    case 'h':
        editor_move(e, 0, -1);
        break;

    case 'j':
        editor_move(e, 1, 0);
        break;

    case 'k':
        editor_move(e, -1, 0);
        break;

    case 'l':
        editor_move(e, 0, 1);
        break;

    case KEY_UP:
        editor_move(e, -1, 0);
        break;

    case KEY_DOWN:
        editor_move(e, 1, 0);
        break;

    case KEY_LEFT:
        editor_move(e, 0, -1);
        break;

    case KEY_RIGHT:
        editor_move(e, 0, 1);
        break;

    case KEY_HOME:
        e->col = 0;
        e->preferred_col = 0;
        break;

    case KEY_END:
        editor_move_line_end(e);
        break;

    case KEY_DELETE:
        editor_delete_char(e);
        break;

    case KEY_PGUP:
    case KEY_PGDN: {
        int i;
        int count = e->screen_rows - 3;
        int direction = key == KEY_PGUP ? -1 : 1;

        if (count < 1)
            count = 1;

        for (i = 0; i < count; i++)
            editor_move(e, direction, 0);

        break;
    }

    case '0':
        e->col = 0;
        e->preferred_col = 0;
        break;

    case '$':
        editor_move_line_end(e);
        break;

    case 'i':
        e->insert_mode = 1;
        break;

    case 'a':
        if (e->col < strlen(e->lines[e->row]))
            e->col++;

        e->insert_mode = 1;
        break;

    case 'A':
        /* Append at end of line: insert_mode first so the cursor may
         * rest past the last byte. */
        e->insert_mode = 1;
        editor_move_line_end(e);
        break;

    case 'I': {
        const char *line = e->lines[e->row];

        e->col = 0;

        while (line[e->col] == ' ' || line[e->col] == '\t')
            e->col++;

        e->preferred_col = e->col;
        e->insert_mode = 1;
        break;
    }

    case 'o':
        editor_open_line(e);
        e->insert_mode = 1;
        break;

    case 'O':
        editor_open_line_above(e);
        e->insert_mode = 1;
        break;

    case 'G':
        if (e->line_count > 0)
            e->row = e->line_count - 1;

        break;

    case 'x':
        editor_delete_char(e);
        break;

    case 'd':
        e->pending = PENDING_DELETE;
        editor_set_status(e, "d");
        break;

    case 'y':
        e->pending = PENDING_YANK;
        editor_set_status(e, "y");
        break;

    case 'p':
        editor_paste(e);
        break;

    case ':':
        e->command_mode = 1;
        e->command_len = 0;
        e->command[0] = '\0';
        break;

    case 27:
    case 3:     /* ESC or Ctrl-C */
        clear_pending(e);
        break;

    default:
        break;
    }
}

void command_execute(Editor *e)
{
    e->command[e->command_len] = '\0';

    if (strcmp(e->command, "w") == 0) {
        (void)editor_save(e);
    } else if (strcmp(e->command, "q") == 0) {
        if (e->dirty)
            editor_set_status(e, "Unsaved changes; use :q!");
        else
            e->running = 0;
    } else if (strcmp(e->command, "q!") == 0) {
        e->running = 0;
    } else if (strcmp(e->command, "wq") == 0) {
        if (editor_save(e) == 0)
            e->running = 0;
    } else if (strcmp(e->command, "syntax on") == 0) {
#if RVI_ENABLE_SYNTAX
        e->syntax_enabled = 1;
        editor_set_status(e, "Syntax highlighting enabled");
#else
        editor_set_status(e, "Syntax highlighting not compiled");
#endif
    } else if (strcmp(e->command, "syntax off") == 0) {
        e->syntax_enabled = 0;
        editor_set_status(e, "Syntax highlighting disabled");
    } else {
        editor_set_status(e, "Unknown command");
    }

    e->command_mode = 0;
    e->command_len = 0;
    e->command[0] = '\0';
}
