#ifndef RVI_COMMANDS_H
#define RVI_COMMANDS_H

#include <stddef.h>

typedef enum PendingCommand {
    PENDING_NONE,
    PENDING_DELETE,
    PENDING_YANK
} PendingCommand;

typedef struct Editor {
    char **lines;
    size_t line_count;
    size_t capacity;

    size_t row;
    size_t col;
    size_t preferred_col;

    size_t row_offset;
    size_t col_offset;
    int screen_rows;
    int screen_cols;

    int insert_mode;
    int dirty;
    int running;

    PendingCommand pending;
    int command_mode;
    char command[128];
    size_t command_len;

    char *filename;
    char status[160];

    char *yank_buffer;
    int yank_linewise;

    int syntax_enabled;
} Editor;

void command_handle_normal(Editor *e, int key);
void command_execute(Editor *e);

void editor_move(Editor *e, int drow, int dcol);
void editor_move_line_end(Editor *e);

void editor_delete_char(Editor *e);
void editor_delete_line(Editor *e);
void editor_delete_word(Editor *e);
void editor_delete_to_end(Editor *e);

void editor_yank_line(Editor *e);
void editor_yank_word(Editor *e);
void editor_yank_to_end(Editor *e);
void editor_paste(Editor *e);

void editor_open_line(Editor *e);
void editor_open_line_above(Editor *e);
int editor_save(Editor *e);
void editor_set_status(Editor *e, const char *message);

#endif
