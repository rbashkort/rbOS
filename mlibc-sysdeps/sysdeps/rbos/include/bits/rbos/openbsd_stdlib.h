#ifndef _RBOS_BITS_OPENBSD_STDLIB_H
#define _RBOS_BITS_OPENBSD_STDLIB_H

/*
 * OpenBSD-compatible declarations that belong in <stdlib.h>.
 *
 * This header is included by mlibc's <stdlib.h> only for rbOS builds
 * (__rbos__) and only when either _DEFAULT_SOURCE or the explicit
 * __RBOS_OPENBSD_COMPAT__ opt-in is defined.  Therefore normal Linux
 * programs do not acquire the OpenBSD names unless they ask for them.
 */

#include <stddef.h>
#include <stdint.h>

#ifndef __MLIBC_ABI_ONLY

#ifdef __cplusplus
extern "C" {
#endif

uint32_t arc4random(void);
void arc4random_buf(void *buf, size_t len);
uint32_t arc4random_uniform(uint32_t upper_bound);

#ifdef __cplusplus
}
#endif

#endif /* !__MLIBC_ABI_ONLY */

#endif /* _RBOS_BITS_OPENBSD_STDLIB_H */
