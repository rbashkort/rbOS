#ifndef RBOS_RBOS_H
#define RBOS_RBOS_H

/*
 * Reserved for rbOS-specific, experimental APIs that have no standard or
 * OpenBSD counterpart.
 *
 * OpenBSD-compatible APIs deliberately live in their standard headers:
 *   pledge(2)      -> <unistd.h>, opt-in via __RBOS_OPENBSD_COMPAT__
 *   unveil(2)      -> <unistd.h>, same gate
 *   arc4random*(3) -> <stdlib.h>, under _DEFAULT_SOURCE (or the same opt-in)
 * Do not add those names here: one declaration, one header.
 */

#endif /* RBOS_RBOS_H */
