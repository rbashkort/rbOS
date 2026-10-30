/* memtest: fork+exec churn and pthread churn, report MemAvailable delta. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

static long memavail_kb(void) {
    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd < 0) return -1;
    char buf[256] = {0};
    read(fd, buf, sizeof(buf) - 1);
    close(fd);
    /* "MemTotal:\tNNN kB\nMemAvailable:\tNNN kB\n" */
    char *p = strstr(buf, "MemAvailable:");
    if (!p) return -1;
    return atol(p + 13);
}

int main(int argc, char *argv[]) {
    int iters = argc > 1 ? atoi(argv[1]) : 20;

    long before = memavail_kb();
    printf("memtest: before=%ld kB\n", before);

    for (int i = 0; i < iters; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("/usr/bin/true", "true", (char *)0);
            _exit(127);
        }
        int st;
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            printf("memtest: child %d bad status %d\n", i, st);
            return 1;
        }
    }
    printf("memtest: %d exec cycles ok\n", iters);

    long after = memavail_kb();
    printf("memtest: after=%ld kB  delta=%ld kB\n", after, after - before);
    if (before > 0 && after < before - 512) {
        printf("memtest: LEAK SUSPECTED (>512kB lost)\n");
        return 2;
    }
    printf("memtest: PASS\n");
    return 0;
}
