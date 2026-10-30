#include "syntax.h"

#if RVI_ENABLE_SYNTAX

#include <ctype.h>
#include <string.h>
#include <unistd.h>

#define COLOR_RESET   "\x1b[0m"
#define COLOR_KEYWORD "\x1b[1;35m"
#define COLOR_STRING  "\x1b[32m"
#define COLOR_NUMBER  "\x1b[36m"
#define COLOR_COMMENT "\x1b[90m"
#define COLOR_PREPROC "\x1b[33m"

typedef enum SyntaxKind {
    SYNTAX_NORMAL,
    SYNTAX_KEYWORD,
    SYNTAX_STRING,
    SYNTAX_NUMBER,
    SYNTAX_COMMENT,
    SYNTAX_PREPROCESSOR
} SyntaxKind;

static const char *const keywords[] = {
    "auto", "break", "case", "char", "const", "continue",
    "default", "do", "double", "else", "enum", "extern",
    "float", "for", "goto", "if", "inline", "int", "long",
    "register", "restrict", "return", "short", "signed",
    "sizeof", "static", "struct", "switch", "typedef",
    "union", "unsigned", "void", "volatile", "while",
    "_Bool", "_Complex", "_Imaginary"
};

static void write_bytes(const char *text, size_t length)
{
    while (length > 0) {
        ssize_t written = write(STDOUT_FILENO, text, length);

        if (written <= 0)
            return;

        text += (size_t)written;
        length -= (size_t)written;
    }
}

static void write_literal(const char *text)
{
    write_bytes(text, strlen(text));
}

static int is_identifier_start(unsigned char c)
{
    return isalpha(c) || c == '_';
}

static int is_identifier_char(unsigned char c)
{
    return isalnum(c) || c == '_';
}

static int is_keyword(const char *word, size_t length)
{
    size_t i;

    for (i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
        if (strlen(keywords[i]) == length &&
            memcmp(keywords[i], word, length) == 0) {
            return 1;
        }
    }

    return 0;
}

static const char *color_for(SyntaxKind kind)
{
    switch (kind) {
    case SYNTAX_KEYWORD:
        return COLOR_KEYWORD;
    case SYNTAX_STRING:
        return COLOR_STRING;
    case SYNTAX_NUMBER:
        return COLOR_NUMBER;
    case SYNTAX_COMMENT:
        return COLOR_COMMENT;
    case SYNTAX_PREPROCESSOR:
        return COLOR_PREPROC;
    case SYNTAX_NORMAL:
    default:
        return COLOR_RESET;
    }
}

static void emit_range(const char *line,
                       size_t token_start,
                       size_t token_end,
                       size_t visible_start,
                       size_t visible_end,
                       SyntaxKind kind,
                       SyntaxKind *active_kind)
{
    size_t begin;
    size_t end;

    if (token_end <= visible_start || token_start >= visible_end)
        return;

    begin = token_start > visible_start ? token_start : visible_start;
    end = token_end < visible_end ? token_end : visible_end;

    if (begin >= end)
        return;

    if (*active_kind != kind) {
        write_literal(color_for(kind));
        *active_kind = kind;
    }

    write_bytes(line + begin, end - begin);
}

static size_t scan_quoted(const char *line, size_t i, char quote)
{
    i++;

    while (line[i] != '\0') {
        if (line[i] == '\\' && line[i + 1] != '\0') {
            i += 2;
            continue;
        }

        if (line[i] == quote) {
            i++;
            break;
        }

        i++;
    }

    return i;
}

static size_t scan_number(const char *line, size_t i)
{
    while (line[i] != '\0') {
        unsigned char c = (unsigned char)line[i];

        if (!isalnum(c) && c != '.' && c != '_' &&
            c != '+' && c != '-') {
            break;
        }

        i++;
    }

    return i;
}

void syntax_draw_line(const char *line, size_t start, size_t width)
{
    size_t length;
    size_t visible_end;
    size_t i = 0;
    int only_whitespace = 1;
    SyntaxKind active_kind = SYNTAX_NORMAL;

    if (!line || width == 0)
        return;

    length = strlen(line);

    if (start >= length)
        return;

    visible_end = start + width;
    if (visible_end < start || visible_end > length)
        visible_end = length;

    while (i < length) {
        size_t token_start = i;
        size_t token_end;
        SyntaxKind kind = SYNTAX_NORMAL;
        unsigned char c = (unsigned char)line[i];

        if (only_whitespace && c == '#') {
            token_end = length;
            kind = SYNTAX_PREPROCESSOR;
        } else if (c == '/' && line[i + 1] == '/') {
            token_end = length;
            kind = SYNTAX_COMMENT;
        } else if (c == '"' || c == '\'') {
            token_end = scan_quoted(line, i, (char)c);
            kind = SYNTAX_STRING;
        } else if (isdigit(c) ||
                   (c == '.' &&
                    isdigit((unsigned char)line[i + 1]))) {
            token_end = scan_number(line, i);
            kind = SYNTAX_NUMBER;
        } else if (is_identifier_start(c)) {
            token_end = i + 1;

            while (is_identifier_char(
                       (unsigned char)line[token_end])) {
                token_end++;
            }

            if (is_keyword(line + i, token_end - i))
                kind = SYNTAX_KEYWORD;
        } else {
            token_end = i + 1;
        }

        emit_range(line, token_start, token_end,
                   start, visible_end, kind, &active_kind);

        if (!isspace(c))
            only_whitespace = 0;

        i = token_end;

        if (i >= visible_end)
            break;
    }

    if (active_kind != SYNTAX_NORMAL)
        write_literal(COLOR_RESET);
}

#endif
