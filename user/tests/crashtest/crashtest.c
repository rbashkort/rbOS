#include <stdio.h>

/* Three flavors of userspace crash: null deref (page fault),
   wild jump (NX/unmapped exec) and a wild write. The kernel must
   kill THIS process only; the system keeps running. */
static void jump_to_nowhere(void);

int main(int argc, char *argv[]) {
    int mode = argc > 1 ? argv[1][0] - '0' : 0;
    if (mode == 1) {
        volatile int (*f)(void) = (volatile int (*)(void))0x402308ULL;
        (void)f;
        ((int (*)(void))0x402308)();
    } else if (mode == 2) {
        volatile int *p = (volatile int *)0x7FFF00000000ULL;
        *p = 42;
        printf("unreachable %d\n", *p);
    }
    volatile int *p = (volatile int *)0;
    printf("crashtest: about to dereference NULL\n");
    *p = 42;
    return 0;
}

static void jump_to_nowhere(void) { jump_to_nowhere(); }
