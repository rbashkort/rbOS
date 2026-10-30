#ifndef RVI_SYNTAX_H
#define RVI_SYNTAX_H

#include <stddef.h>

#ifndef RVI_ENABLE_SYNTAX
#define RVI_ENABLE_SYNTAX 1
#endif

#if RVI_ENABLE_SYNTAX

void syntax_draw_line(const char *line, size_t start, size_t width);

#else

#include <unistd.h>

static void syntax_draw_line(const char *line, size_t start, size_t width)
{
    size_t length = 0;
    size_t visible;

    while (line[length] != '\0')
        length++;

    if (start >= length)
        return;

    visible = length - start;
    if (visible > width)
        visible = width;

    (void)write(STDOUT_FILENO, line + start, visible);
}

#endif

#endif
