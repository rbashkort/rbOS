/* Static TLS: the main thread and a pthread must see separate __thread
 * instances. Host glibc lays out PT_TLS/TCB itself, so this exercises the
 * kernel's initial-thread TLS setup rather than mlibc's. */
#include <pthread.h>
#include <stdio.h>

static __thread int tv = 7;

static void *fn(void *arg) {
    (void)arg;
    tv = 42;
    return (void *)(long)tv;
}

int main(void) {
    pthread_t t;
    void *r = NULL;

    tv = 7;
    if (pthread_create(&t, NULL, fn, NULL) != 0)
        return 1;
    pthread_join(t, &r);

    printf("TLS-OK main=%d thread=%ld\n", tv, (long)r);
    return (tv == 7 && (long)r == 42) ? 0 : 1;
}
