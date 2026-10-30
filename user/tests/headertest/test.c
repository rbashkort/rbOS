/*
 * P7.0b: ordinary rbOS programs must not acquire the OpenBSD names.
 *
 * This file is compiled WITHOUT __RBOS_OPENBSD_COMPAT__.  It includes the
 * standard headers and deliberately declares variables named pledge/unveil;
 * if the opt-in hook leaked into the default view, these would collide with
 * the function declarations and the build would fail.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef __rbos__
#error "rbos-cc must define __rbos__"
#endif

#ifdef __RBOS_OPENBSD_COMPAT__
#error "headertest must be built without __RBOS_OPENBSD_COMPAT__"
#endif

int pledge;
int unveil;

int main(void) {
    /* arc4random is visible by default via _DEFAULT_SOURCE, matching glibc. */
    uint32_t v = arc4random_uniform(2);
    if (v >= 2) {
        printf("HEADER-FAIL: arc4random_uniform(2) = %u\n", (unsigned)v);
        return 1;
    }

    printf("HEADER-OK\n");
    return 0;
}
