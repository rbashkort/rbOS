#ifndef _RBOS_BITS_OPENBSD_UNISTD_H
#define _RBOS_BITS_OPENBSD_UNISTD_H

/*
 * OpenBSD-compatible declarations that belong in <unistd.h>.
 *
 * This header is included by mlibc's <unistd.h> only when both __rbos__ and
 * __RBOS_OPENBSD_COMPAT__ are defined.  The latter is opt-in: it is added by
 * ports/rbos-cc when RBOS_OPENBSD_COMPAT=1, so ordinary Linux programs do
 * not get the OpenBSD names in their namespace.
 */

#ifndef __MLIBC_ABI_ONLY

#ifdef __cplusplus
extern "C" {
#endif

int pledge(const char *promises, const char *execpromises);
int unveil(const char *path, const char *permissions);

#ifdef __cplusplus
}
#endif

#endif /* !__MLIBC_ABI_ONLY */

#endif /* _RBOS_BITS_OPENBSD_UNISTD_H */
