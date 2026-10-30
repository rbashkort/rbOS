#include "commands.h"
#include "syntax.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define WRITE_LITERAL(fd, text) \
    ((void)write((fd), (text), sizeof(text) - 1))

#define TAB_STOP 4

#define KEY_UP    1000
#define KEY_DOWN  1001
#define KEY_RIGHT 1002
#define KEY_LEFT  1003
#define KEY_HOME  1004
#define KEY_END   1005
#define KEY_DELETE 1006
#define KEY_PGUP  1007
#define KEY_PGDN  1008

#define KEY_UNKNOWN -2

static struct termios original_termios;
static int raw_mode_enabled;

static void die(const char *message)
{
    WRITE_LITERAL(STDOUT_FILENO, "\x1b[2J\x1b[H");
    perror(message);
    exit(1);
}

static void disable_raw_mode(void)
{
    if (raw_mode_enabled) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH,
                        &original_termios);
        raw_mode_enabled = 0;
    }
}

static void enable_raw_mode(void)
{
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &original_termios) == -1)
        die("tcgetattr");

    raw = original_termios;

    raw.c_iflag &= (tcflag_t)~(
        BRKINT | ICRNL | INPCK | ISTRIP | IXON
    );
    raw.c_oflag &= (tcflag_t)~OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= (tcflag_t)~(
        ECHO | ICANON | IEXTEN | ISIG
    );

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");

    raw_mode_enabled = 1;
    atexit(disable_raw_mode);
}

static char *copy_string(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);

    if (!copy)
        die("malloc");

    memcpy(copy, text, length);
    return copy;
}

static void ensure_line_capacity(Editor *e)
{
    char **new_lines;
    size_t new_capacity;

    if (e->line_count < e->capacity)
        return;

    new_capacity = e->capacity ? e->capacity * 2 : 16;
    new_lines = realloc(
        e->lines,
        new_capacity * sizeof(*new_lines)
    );

    if (!new_lines)
        die("realloc");

    e->lines = new_lines;
    e->capacity = new_capacity;
}

static void insert_line(Editor *e,
                        size_t index,
                        const char *text)
{
    ensure_line_capacity(e);

    if (index < e->line_count) {
        memmove(
            &e->lines[index + 1],
            &e->lines[index],
            (e->line_count - index) *
                sizeof(*e->lines)
        );
    }

    e->lines[index] = copy_string(text);
    e->line_count++;
}

static void load_file(Editor *e, const char *filename)
{
    FILE *file;
    char *line;
    size_t length = 0;
    size_t capacity = 128;
    int c;

    e->filename = copy_string(filename);
    file = fopen(filename, "r");

    if (!file) {
        if (errno == ENOENT) {
            insert_line(e, 0, "");
            editor_set_status(e, "New file");
            return;
        }

        die("fopen");
    }

    line = malloc(capacity);

    if (!line) {
        fclose(file);
        die("malloc");
    }

    while ((c = fgetc(file)) != EOF) {
        if (c == '\r')
            continue;

        if (c == '\n') {
            line[length] = '\0';
            insert_line(e, e->line_count, line);
            length = 0;
            continue;
        }

        if (length + 1 >= capacity) {
            char *new_line;
            size_t new_capacity = capacity * 2;

            new_line = realloc(line, new_capacity);

            if (!new_line) {
                free(line);
                fclose(file);
                die("realloc");
            }

            line = new_line;
            capacity = new_capacity;
        }

        line[length++] = (char)c;
    }

    if (ferror(file)) {
        free(line);
        fclose(file);
        die("fgetc");
    }

    if (length > 0 || e->line_count == 0) {
        line[length] = '\0';
        insert_line(e, e->line_count, line);
    }

    free(line);
    fclose(file);
}

static void get_window_size(Editor *e)
{
    struct winsize size;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1 ||
        size.ws_col == 0 || size.ws_row == 0) {
        e->screen_rows = 24;
        e->screen_cols = 80;
    } else {
        e->screen_rows = size.ws_row;
        e->screen_cols = size.ws_col;
    }
}

void editor_set_status(Editor *e, const char *message)
{
    (void)snprintf(
        e->status,
        sizeof(e->status),
        "%s",
        message
    );
}

/* UTF-8 continuation byte (10xxxxxx): never a cursor stop on its own. */
static int is_utf8_cont(unsigned char c)
{
    return (c & 0xc0) == 0x80;
}

/* Screen width of line[0 .. byte_pos): '\t' jumps to the next TAB_STOP
 * column, UTF-8 continuation bytes add no width. */
static size_t byte_to_visual(const char *line, size_t byte_pos)
{
    size_t visual = 0;
    size_t i;

    for (i = 0; i < byte_pos && line[i] != '\0'; i++) {
        unsigned char c = (unsigned char)line[i];

        if (c == '\t')
            visual += (size_t)TAB_STOP - (visual % (size_t)TAB_STOP);
        else if (!is_utf8_cont(c))
            visual++;
    }

    return visual;
}

static char *expand_tabs(const char *line)
{
    size_t length = strlen(line);
    size_t tabs = 0;
    size_t i;
    size_t visual = 0;
    size_t out_len = 0;
    char *out;
    char *p;

    for (i = 0; i < length; i++) {
        if (line[i] == '\t')
            tabs++;
    }

    if (tabs == 0)
        return copy_string(line);

    out = malloc(length + tabs * (TAB_STOP - 1) + 1);

    if (!out)
        die("malloc");

    p = out;

    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)line[i];

        if (c == '\t') {
            size_t width =
                (size_t)TAB_STOP - (visual % (size_t)TAB_STOP);

            memset(p, ' ', width);
            p += width;
            visual += width;
            out_len += width;
        } else {
            *p++ = line[i];
            out_len++;

            if (!is_utf8_cont(c))
                visual++;
        }
    }

    *p = '\0';
    return out;
}

static size_t line_length(const Editor *e, size_t row)
{
    return strlen(e->lines[row]);
}

static void clamp_cursor(Editor *e)
{
    size_t length;
    size_t maximum;

    if (e->line_count == 0)
        return;

    if (e->row >= e->line_count)
        e->row = e->line_count - 1;

    length = line_length(e, e->row);

    if (e->insert_mode) {
        if (e->col > length)
            e->col = length;
    } else {
        maximum = length ? length - 1 : 0;

        if (e->col > maximum)
            e->col = maximum;

        /* Never park the cursor on a UTF-8 continuation byte. */
        while (e->col > 0 &&
               is_utf8_cont(
                   (unsigned char)e->lines[e->row][e->col])) {
            e->col--;
        }
    }
}

void editor_move(Editor *e, int drow, int dcol)
{
    if (drow < 0 && e->row > 0) {
        e->row--;
        e->col = e->preferred_col;
    } else if (drow > 0 &&
               e->row + 1 < e->line_count) {
        e->row++;
        e->col = e->preferred_col;
    }

    if (dcol < 0 && e->col > 0) {
        const char *line = e->lines[e->row];

        e->col--;

        while (e->col > 0 &&
               is_utf8_cont((unsigned char)line[e->col])) {
            e->col--;
        }
    } else if (dcol > 0) {
        const char *line = e->lines[e->row];
        size_t length = strlen(line);
        /* In insert mode the cursor may rest past the last byte so
         * freshly typed text lands at end of line; in normal mode it
         * stays on a real character. */
        size_t maximum =
            e->insert_mode ? length : (length ? length - 1 : 0);

        if (e->col < maximum) {
            e->col++;

            while (e->col < maximum &&
                   is_utf8_cont((unsigned char)line[e->col])) {
                e->col++;
            }
        }
    }

    clamp_cursor(e);
    e->preferred_col = e->col;
}

void editor_move_line_end(Editor *e)
{
    size_t length = line_length(e, e->row);

    if (e->insert_mode)
        e->col = length;
    else
        e->col = length ? length - 1 : 0;

    e->preferred_col = e->col;
}

static void replace_line(Editor *e,
                         size_t row,
                         char *replacement)
{
    free(e->lines[row]);
    e->lines[row] = replacement;
    e->dirty = 1;
}

static void insert_character(Editor *e, int c)
{
    char *line = e->lines[e->row];
    size_t length = strlen(line);
    char *replacement = malloc(length + 2);

    if (!replacement)
        die("malloc");

    memcpy(replacement, line, e->col);
    replacement[e->col] = (char)c;

    memcpy(
        replacement + e->col + 1,
        line + e->col,
        length - e->col + 1
    );

    replace_line(e, e->row, replacement);
    e->col++;
    e->preferred_col = e->col;
}

/* Tab inserts spaces up to the next TAB_STOP column ("tab = N spaces"
 * with expandtab semantics), so indentation is always plain spaces. */
static void insert_tab(Editor *e)
{
    char *line = e->lines[e->row];
    size_t length = strlen(line);
    size_t visual = byte_to_visual(line, e->col);
    size_t count = (size_t)TAB_STOP - (visual % (size_t)TAB_STOP);
    char *replacement = malloc(length + count + 1);

    if (!replacement)
        die("malloc");

    memcpy(replacement, line, e->col);
    memset(replacement + e->col, ' ', count);

    memcpy(
        replacement + e->col + count,
        line + e->col,
        length - e->col + 1
    );

    replace_line(e, e->row, replacement);
    e->col += count;
    e->preferred_col = e->col;
}

static void insert_newline(Editor *e)
{
    char *line = e->lines[e->row];
    char *left = malloc(e->col + 1);
    char *right;
    size_t indent = 0;
    size_t extra = 0;
    size_t i;

    if (!left)
        die("malloc");

    memcpy(left, line, e->col);
    left[e->col] = '\0';

    while (line[indent] == ' ' || line[indent] == '\t')
        indent++;

    for (i = e->col; i > 0; i--) {
        char c = left[i - 1];

        if (c == ' ' || c == '\t')
            continue;

        if (c == '{')
            extra = (size_t)TAB_STOP;

        break;
    }

    const char *tail = line + e->col +
        (e->col < indent ? indent - e->col : 0);

    right = malloc(
        indent + extra + strlen(tail) + 1
    );

    if (!right) {
        free(left);
        die("malloc");
    }

    memcpy(right, line, indent);
    memset(right + indent, ' ', extra);
    strcpy(right + indent + extra, tail);

    free(e->lines[e->row]);
    e->lines[e->row] = left;

    insert_line(e, e->row + 1, right);
    free(right);

    e->row++;
    e->col = indent + extra;
    e->preferred_col = e->col;
    e->dirty = 1;
}

static void insert_backspace(Editor *e)
{
    char *line;
    size_t length;
    char *replacement;

    if (e->col == 0) {
        size_t previous_length;
        size_t current_length;
        char *joined;

        if (e->row == 0)
            return;

        previous_length = line_length(e, e->row - 1);
        current_length = line_length(e, e->row);

        joined = malloc(
            previous_length + current_length + 1
        );

        if (!joined)
            die("malloc");

        memcpy(
            joined,
            e->lines[e->row - 1],
            previous_length
        );

        memcpy(
            joined + previous_length,
            e->lines[e->row],
            current_length + 1
        );

        free(e->lines[e->row - 1]);
        free(e->lines[e->row]);

        e->lines[e->row - 1] = joined;

        memmove(
            &e->lines[e->row],
            &e->lines[e->row + 1],
            (e->line_count - e->row - 1) *
                sizeof(*e->lines)
        );

        e->line_count--;
        e->row--;
        e->col = previous_length;
        e->preferred_col = e->col;
        e->dirty = 1;
        return;
    }

    line = e->lines[e->row];
    length = strlen(line);

    /* Soft-tab unindent: if the cursor sits on a tabstop and only spaces
     * stand between it and the previous tabstop, erase them as a group. */
    {
        size_t visual = byte_to_visual(line, e->col);
        size_t want =
            visual % (size_t)TAB_STOP ?
            visual % (size_t)TAB_STOP : (size_t)TAB_STOP;
        size_t spaces = 0;

        while (spaces < want &&
               spaces < e->col &&
               line[e->col - 1 - spaces] == ' ') {
            spaces++;
        }

        if (spaces == want && spaces > 1) {
            replacement = malloc(length - spaces + 1);

            if (!replacement)
                die("malloc");

            memcpy(replacement, line, e->col - spaces);

            memcpy(
                replacement + e->col - spaces,
                line + e->col,
                length - e->col + 1
            );

            replace_line(e, e->row, replacement);
            e->col -= spaces;
            e->preferred_col = e->col;
            return;
        }
    }

    replacement = malloc(length);

    if (!replacement)
        die("malloc");
    {
        size_t erase = 1;

        while (erase < e->col &&
               is_utf8_cont(
                   (unsigned char)line[e->col - erase])) {
            erase++;
        }

        memcpy(replacement, line, e->col - erase);

        memcpy(
            replacement + e->col - erase,
            line + e->col,
            length - e->col + 1
        );

        replace_line(e, e->row, replacement);
        e->col -= erase;
        e->preferred_col = e->col;
    }
}

void editor_delete_char(Editor *e)
{
    char *line = e->lines[e->row];
    size_t length = strlen(line);
    char *replacement;
    size_t erase = 1;

    if (length == 0 || e->col >= length)
        return;

    while (e->col + erase < length &&
           is_utf8_cont(
               (unsigned char)line[e->col + erase])) {
        erase++;
    }

    replacement = malloc(length - erase + 1);

    if (!replacement)
        die("malloc");

    memcpy(replacement, line, e->col);

    memcpy(
        replacement + e->col,
        line + e->col + erase,
        length - e->col - erase + 1
    );

    replace_line(e, e->row, replacement);
    clamp_cursor(e);
}

void editor_delete_line(Editor *e)
{
    free(e->lines[e->row]);

    if (e->line_count == 1) {
        e->lines[0] = copy_string("");
    } else {
        memmove(
            &e->lines[e->row],
            &e->lines[e->row + 1],
            (e->line_count - e->row - 1) *
                sizeof(*e->lines)
        );

        e->line_count--;

        if (e->row >= e->line_count)
            e->row = e->line_count - 1;
    }

    e->col = 0;
    e->preferred_col = 0;
    e->dirty = 1;
}

static size_t word_end(const char *line, size_t start)
{
    size_t end = start;
    size_t length = strlen(line);

    if (end >= length)
        return end;

    while (end < length &&
           line[end] != ' ' &&
           line[end] != '\t') {
        end++;
    }

    while (end < length &&
           (line[end] == ' ' ||
            line[end] == '\t')) {
        end++;
    }

    return end;
}

void editor_delete_word(Editor *e)
{
    char *line = e->lines[e->row];
    size_t length = strlen(line);
    size_t end = word_end(line, e->col);
    char *replacement;

    if (end <= e->col)
        return;

    replacement = malloc(
        length - (end - e->col) + 1
    );

    if (!replacement)
        die("malloc");

    memcpy(replacement, line, e->col);

    memcpy(
        replacement + e->col,
        line + end,
        length - end + 1
    );

    replace_line(e, e->row, replacement);
    clamp_cursor(e);
}

void editor_delete_to_end(Editor *e)
{
    char *line = e->lines[e->row];
    size_t length = strlen(line);
    char *replacement;

    if (e->col >= length)
        return;

    replacement = malloc(e->col + 1);

    if (!replacement)
        die("malloc");

    memcpy(replacement, line, e->col);
    replacement[e->col] = '\0';

    replace_line(e, e->row, replacement);
    clamp_cursor(e);
}

static void set_yank(Editor *e,
                     const char *text,
                     size_t length,
                     int linewise)
{
    char *copy = malloc(length + 1);

    if (!copy)
        die("malloc");

    memcpy(copy, text, length);
    copy[length] = '\0';

    free(e->yank_buffer);
    e->yank_buffer = copy;
    e->yank_linewise = linewise;
}

void editor_yank_line(Editor *e)
{
    const char *line = e->lines[e->row];

    set_yank(e, line, strlen(line), 1);
    editor_set_status(e, "1 line yanked");
}

void editor_yank_word(Editor *e)
{
    const char *line = e->lines[e->row];
    size_t end = word_end(line, e->col);

    if (end <= e->col) {
        editor_set_status(e, "Nothing to yank");
        return;
    }

    set_yank(
        e,
        line + e->col,
        end - e->col,
        0
    );

    editor_set_status(e, "Text yanked");
}

void editor_yank_to_end(Editor *e)
{
    const char *line = e->lines[e->row];
    size_t length = strlen(line);

    if (e->col >= length) {
        editor_set_status(e, "Nothing to yank");
        return;
    }

    set_yank(
        e,
        line + e->col,
        length - e->col,
        0
    );

    editor_set_status(e, "Text yanked");
}

void editor_paste(Editor *e)
{
    char *line;
    char *replacement;
    size_t line_length_value;
    size_t yank_length;
    size_t position;

    if (!e->yank_buffer) {
        editor_set_status(e, "Yank buffer is empty");
        return;
    }

    if (e->yank_linewise) {
        insert_line(
            e,
            e->row + 1,
            e->yank_buffer
        );

        e->row++;
        e->col = 0;
        e->preferred_col = 0;
        e->dirty = 1;
        return;
    }

    line = e->lines[e->row];
    line_length_value = strlen(line);
    yank_length = strlen(e->yank_buffer);

    if (line_length_value == 0)
        position = 0;
    else
        position = e->col + 1;

    if (position > line_length_value)
        position = line_length_value;

    replacement = malloc(
        line_length_value + yank_length + 1
    );

    if (!replacement)
        die("malloc");

    memcpy(replacement, line, position);

    memcpy(
        replacement + position,
        e->yank_buffer,
        yank_length
    );

    memcpy(
        replacement + position + yank_length,
        line + position,
        line_length_value - position + 1
    );

    replace_line(e, e->row, replacement);

    if (yank_length > 0)
        e->col = position + yank_length - 1;

    e->preferred_col = e->col;
}

void editor_open_line(Editor *e)
{
    insert_line(e, e->row + 1, "");

    e->row++;
    e->col = 0;
    e->preferred_col = 0;
    e->dirty = 1;
}

void editor_open_line_above(Editor *e)
{
    insert_line(e, e->row, "");

    e->col = 0;
    e->preferred_col = 0;
    e->dirty = 1;
}

int editor_save(Editor *e)
{
    FILE *file = fopen(e->filename, "w");
    size_t i;

    if (!file) {
        editor_set_status(e, "Cannot save file");
        return -1;
    }

    for (i = 0; i < e->line_count; i++) {
        if (fputs(e->lines[i], file) == EOF) {
            fclose(file);
            editor_set_status(e, "Write error");
            return -1;
        }

        /* POSIX text files end every line (including the last one)
         * with '\n'; editors that omit it produce "No newline at end
         * of file" warnings in diff/gcc. */
        if (fputc('\n', file) == EOF) {
            fclose(file);
            editor_set_status(e, "Write error");
            return -1;
        }
    }

    if (fclose(file) == EOF) {
        editor_set_status(e, "Write error");
        return -1;
    }

    e->dirty = 0;
    editor_set_status(e, "Written");
    return 0;
}

static void scroll_editor(Editor *e)
{
    int text_rows = e->screen_rows - 2;

    if (text_rows < 1)
        text_rows = 1;

    if (e->row < e->row_offset)
        e->row_offset = e->row;

    if (e->row >=
        e->row_offset + (size_t)text_rows) {
        e->row_offset =
            e->row - (size_t)text_rows + 1;
    }

    if (e->col < e->col_offset) {
        e->col_offset = e->col;
    } else if (e->line_count > 0) {
        /* Compare screen columns, not bytes: a tab takes TAB_STOP
         * cells while occupying a single byte. */
        const char *line = e->lines[e->row];
        size_t cursor_v = byte_to_visual(line, e->col);

        while (cursor_v -
                   byte_to_visual(line, e->col_offset) >=
               (size_t)e->screen_cols) {
            e->col_offset++;

            while (e->lines[e->row][e->col_offset] &&
                   is_utf8_cont(
                       (unsigned char)
                           e->lines[e->row][e->col_offset])) {
                e->col_offset++;
            }
        }
    } else if (e->col >=
               e->col_offset + (size_t)e->screen_cols) {
        e->col_offset =
            e->col - (size_t)e->screen_cols + 1;
    }
}

static void draw_text_line(Editor *e, size_t file_row)
{
    const char *line = e->lines[file_row];
    /* Render tabs as spaces so a tab never desyncs the cursor and the
     * terminal never jumps to its own 8-column stops. */
    char *expanded = expand_tabs(line);
    size_t expanded_length = strlen(expanded);
    size_t start = 0;
    size_t walked_bytes = 0;
    size_t walked_visual = 0;
    size_t end;
    size_t used = 0;

    while (walked_bytes < e->col_offset &&
           line[walked_bytes] != '\0') {
        unsigned char c =
            (unsigned char)line[walked_bytes];

        if (c == '\t') {
            size_t width = (size_t)TAB_STOP -
                (walked_visual % (size_t)TAB_STOP);

            start += width;
            walked_visual += width;
        } else {
            start++;

            if (!is_utf8_cont(c))
                walked_visual++;
        }

        walked_bytes++;
    }

    if (start >= expanded_length) {
        free(expanded);
        return;
    }

    end = start;

    while (end < expanded_length &&
           used < (size_t)e->screen_cols) {
        unsigned char c = (unsigned char)expanded[end];

        if (!is_utf8_cont(c))
            used++;

        end++;

        while (end < expanded_length &&
               is_utf8_cont((unsigned char)expanded[end])) {
            end++;
        }
    }

#if RVI_ENABLE_SYNTAX
    if (e->syntax_enabled) {
        syntax_draw_line(expanded, start, end - start);
        free(expanded);
        return;
    }
#endif

    (void)write(
        STDOUT_FILENO,
        expanded + start,
        end - start
    );

    free(expanded);
}

static void draw_editor(Editor *e)
{
    int y;
    int text_rows;
    char buffer[512];

    /* Re-query every frame so a terminal resize (SIGWINCH) takes
     * effect immediately without signal handling. */
    get_window_size(e);
    text_rows = e->screen_rows - 2;

    if (text_rows < 1)
        text_rows = 1;

    scroll_editor(e);

    WRITE_LITERAL(
        STDOUT_FILENO,
        "\x1b[?25l\x1b[H"
    );

    for (y = 0; y < text_rows; y++) {
        size_t file_row =
            e->row_offset + (size_t)y;

        WRITE_LITERAL(STDOUT_FILENO, "\x1b[2K");

        if (file_row < e->line_count)
            draw_text_line(e, file_row);
        else
            WRITE_LITERAL(STDOUT_FILENO, "~");

        WRITE_LITERAL(STDOUT_FILENO, "\r\n");
    }

    WRITE_LITERAL(
        STDOUT_FILENO,
        "\x1b[7m\x1b[2K"
    );

    (void)snprintf(
        buffer,
        sizeof(buffer),
        " rvi | %s%s | %s | syntax:%s | %lu:%lu ",
        e->filename,
        e->dirty ? " [+]" : "",
        e->insert_mode ? "INSERT" : "NORMAL",
        e->syntax_enabled ? "on" : "off",
        (unsigned long)e->row + 1,
        (unsigned long)e->col + 1
    );

    (void)write(
        STDOUT_FILENO,
        buffer,
        strlen(buffer)
    );

    WRITE_LITERAL(
        STDOUT_FILENO,
        "\x1b[m\r\n\x1b[2K"
    );

    if (e->command_mode) {
        WRITE_LITERAL(STDOUT_FILENO, ":");

        (void)write(
            STDOUT_FILENO,
            e->command,
            e->command_len
        );
    } else {
        (void)write(
            STDOUT_FILENO,
            e->status,
            strlen(e->status)
        );
    }

    /* Cursor screen column is visual (tabs expanded), not byte-based. */
    {
        const char *line =
            e->line_count > 0 ? e->lines[e->row] : "";
        size_t cursor_x =
            byte_to_visual(line, e->col) -
            byte_to_visual(line, e->col_offset) + 1;

        (void)snprintf(
            buffer,
            sizeof(buffer),
            "\x1b[%lu;%luH",
            (unsigned long)(
                e->row - e->row_offset + 1
            ),
            (unsigned long)cursor_x
        );
    }

    (void)write(
        STDOUT_FILENO,
        buffer,
        strlen(buffer)
    );

    WRITE_LITERAL(STDOUT_FILENO, "\x1b[?25h");
}

static int read_key(void)
{
    unsigned char c;
    ssize_t count;

    for (;;) {
        count = read(STDIN_FILENO, &c, 1);
        if (count == 1)
            break;
        if (count == -1 && errno == EINTR)
            continue;
        if (count == 0)
            continue;
        return -1;
    }

    if (c != 27)
        return c;

    unsigned char seq[16];
    int n = 0;

    while (n < (int)sizeof(seq)) {
        count = read(STDIN_FILENO, &seq[n], 1);
        if (count == 1) {
            n++;
            if (seq[0] == '[' && n >= 2) {
                unsigned char last = seq[n - 1];
                if ((last >= 'A' && last <= 'Z') ||
                    (last >= 'a' && last <= 'z') ||
                    last == '~')
                    break;
            } else if (seq[0] == 'O' && n >= 2) {
                unsigned char last = seq[n - 1];
                if (last >= 'A' && last <= 'Z')
                    break;
            }
            continue;
        }
        break;
    }

    if (n == 0)
        return 27;

    if (seq[0] == '[') {
        if (n == 3 && seq[2] == '~') {
            switch (seq[1]) {
            case '1':
            case '7': return KEY_HOME;
            case '3': return KEY_DELETE;
            case '4':
            case '8': return KEY_END;
            case '5': return KEY_PGUP;
            case '6': return KEY_PGDN;
            }

            return KEY_UNKNOWN;
        }

        switch (seq[1]) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        }
    } else if (seq[0] == 'O') {
        switch (seq[1]) {
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case 'P':
        case 'Q':
        case 'R':
        case 'S': return KEY_UNKNOWN;
        }
    }

    return KEY_UNKNOWN;
}

static void handle_command_key(Editor *e, int key)
{
    if (key == 27) {
        e->command_mode = 0;
        e->command_len = 0;
        e->command[0] = '\0';
    } else if (key == '\r' || key == '\n') {
        command_execute(e);
    } else if (key == 127 || key == '\b') {
        if (e->command_len > 0) {
            e->command_len--;
            e->command[e->command_len] = '\0';
        } else {
            e->command_mode = 0;
        }
    } else if (key >= 32 && key < 256 && key != 127 &&
               e->command_len + 1 <
                   sizeof(e->command)) {
        e->command[e->command_len++] = (char)key;
        e->command[e->command_len] = '\0';
    }
}

static void handle_insert_key(Editor *e, int key)
{
    if (key == 27 || key == 3) {
        e->insert_mode = 0;

        if (e->col > 0)
            editor_move(e, 0, -1);

        e->preferred_col = e->col;
    } else if (key == KEY_UP) {
        editor_move(e, -1, 0);
    } else if (key == KEY_DOWN) {
        editor_move(e, 1, 0);
    } else if (key == KEY_LEFT) {
        editor_move(e, 0, -1);
    } else if (key == KEY_RIGHT) {
        editor_move(e, 0, 1);
    } else if (key == KEY_HOME) {
        e->col = 0;
        e->preferred_col = 0;
    } else if (key == KEY_END) {
        editor_move_line_end(e);
    } else if (key == KEY_DELETE) {
        editor_delete_char(e);
    } else if (key == KEY_PGUP || key == KEY_PGDN) {
        int i;
        int count = e->screen_rows - 3;
        int direction = key == KEY_PGUP ? -1 : 1;

        if (count < 1)
            count = 1;

        for (i = 0; i < count; i++)
            editor_move(e, direction, 0);
    } else if (key == '\t') {
        insert_tab(e);
    } else if (key == 127 || key == '\b') {
        insert_backspace(e);
    } else if (key == '\r' || key == '\n') {
        insert_newline(e);
    } else if (key >= 32 && key < 256 && key != 127) {
        insert_character(e, key);
    }
}

static void free_editor(Editor *e)
{
    size_t i;

    for (i = 0; i < e->line_count; i++)
        free(e->lines[i]);

    free(e->lines);
    free(e->filename);
    free(e->yank_buffer);
}

int main(int argc, char **argv)
{
    Editor editor;
    int key;

    if (argc != 2) {
        fprintf(stderr, "usage: rvi <file>\n");
        return 1;
    }

    memset(&editor, 0, sizeof(editor));

    editor.running = 1;

#if RVI_ENABLE_SYNTAX
    editor.syntax_enabled = 1;
#else
    editor.syntax_enabled = 0;
#endif

    load_file(&editor, argv[1]);
    get_window_size(&editor);
    enable_raw_mode();

    while (editor.running) {
        draw_editor(&editor);
        key = read_key();

        if (key == -1)
            break;

        if (key == KEY_UNKNOWN)
            continue;

        editor.status[0] = '\0';

        if (editor.command_mode) {
            handle_command_key(&editor, key);
        } else if (editor.insert_mode) {
            handle_insert_key(&editor, key);
        } else {
            command_handle_normal(&editor, key);
        }

        clamp_cursor(&editor);
    }

    WRITE_LITERAL(
        STDOUT_FILENO,
        "\x1b[2J\x1b[H"
    );

    free_editor(&editor);
    return 0;
}
