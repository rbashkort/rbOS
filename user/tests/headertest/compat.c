/*
 * P7.0b compile-only probe: with __RBOS_OPENBSD_COMPAT__, the OpenBSD names
 * must be declared by their standard headers.  It is never linked or run.
 */
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef __rbos__
#error "rbos-cc must define __rbos__"
#endif
#ifndef __RBOS_OPENBSD_COMPAT__
#error "compat probe must define __RBOS_OPENBSD_COMPAT__"
#endif

static int (*pledge_fn)(const char *, const char *) = pledge;
static int (*unveil_fn)(const char *, const char *) = unveil;
static uint32_t (*arc4random_fn)(void) = arc4random;
static uint32_t (*uniform_fn)(uint32_t) = arc4random_uniform;
static void (*buf_fn)(void *, size_t) = arc4random_buf;

int probe(void) {
    return (pledge_fn == 0) | (unveil_fn == 0) | (arc4random_fn == 0) |
           (uniform_fn == 0) | (buf_fn == 0);
}
