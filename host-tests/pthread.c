/* glibc pthreads: mutex contention, condvar broadcast and timedwait.
 * This is the main futex oracle for host-built Linux binaries. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define NTHREADS 4
#define ITERS    20000

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static long counter;
static int phase;

static void *worker(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS; i++) {
        pthread_mutex_lock(&mu);
        counter++;
        pthread_mutex_unlock(&mu);
    }
    return NULL;
}

static void *waiter(void *arg) {
    (void)arg;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
    pthread_mutex_lock(&mu);
    while (phase == 0) {
        int r = pthread_cond_timedwait(&cv, &mu, &ts);
        if (r == ETIMEDOUT) {
            pthread_mutex_unlock(&mu);
            return (void *)1;
        }
    }
    pthread_mutex_unlock(&mu);
    return NULL;
}

int main(void) {
    pthread_t th[NTHREADS], w;
    void *wret = NULL;

    if (pthread_create(&w, NULL, waiter, NULL) != 0) {
        perror("pthread_create(waiter)");
        return 1;
    }
    usleep(100000);

    for (int i = 0; i < NTHREADS; i++) {
        if (pthread_create(&th[i], NULL, worker, NULL) != 0) {
            perror("pthread_create(worker)");
            return 1;
        }
    }
    for (int i = 0; i < NTHREADS; i++)
        pthread_join(th[i], NULL);

    pthread_mutex_lock(&mu);
    phase = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    pthread_join(w, &wret);

    printf("counter=%ld expected=%d waiter=%s\n",
           counter, NTHREADS * ITERS, wret ? "timeout" : "ok");
    if (counter != (long)NTHREADS * ITERS || wret)
        return 1;
    puts("PTHREAD-OK");
    return 0;
}
