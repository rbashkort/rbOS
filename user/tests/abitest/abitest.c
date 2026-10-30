/* abitest: exercise phase 0/1 ABI additions (pipes, dup offsets, getdents64,
 * readv/writev). Prints PASS/FAIL per check and exits with the failure count. */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <dirent.h>
#include <errno.h>

static int fails = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); fails++; } \
} while (0)

int main(void) {
    /* 1. pipe: parent reads what child writes */
    int pfd[2];
    CHECK(pipe(pfd) == 0, "pipe()");

    pid_t pid = fork();
    if (pid == 0) {
        close(pfd[0]);
        const char *msg = "ping";
        write(pfd[1], msg, 4);
        close(pfd[1]); /* EOF for reader */
        _exit(0);
    }
    close(pfd[1]);
    char buf[16];
    memset(buf, 0xAB, sizeof(buf));
    ssize_t r = read(pfd[0], buf, sizeof(buf));
    CHECK(r == 4 && memcmp(buf, "ping", 4) == 0, "pipe data child->parent");
    r = read(pfd[0], buf, sizeof(buf));       /* all writers gone -> EOF */
    CHECK(r == 0, "pipe EOF after writer closes");
    waitpid(pid, NULL, 0);

    /* 2. shared offset through dup (phase 0 core invariant) */
    int f = open("/home/test.txt", O_RDONLY);
    if (f < 0) f = open("/etc/passwd", O_RDONLY);
    if (f >= 0) {
        int g = dup(f);
        char a[8] = {0}, b[8] = {0};
        read(f, a, 4);                 /* advances shared offset to 4 */
        off_t pos = lseek(g, 0, SEEK_CUR);
        CHECK(pos == 4, "dup shares file offset");
        read(g, b, 2);                 /* continues from byte 4 */
        pos = lseek(f, 0, SEEK_CUR);
        CHECK(pos == 6, "offset shared both directions");
        close(g); close(f);
    } else {
        printf("SKIP: no readable file for offset test\n");
    }

    /* 3. dup2 redirects and shares */
    int pair[2]; pipe(pair);
    int saved = dup(1);
    fflush(stdout); /* flush buffered PASS lines to the real stdout first:
                     * without this, under a piped (fully-buffered) stdout
                     * the earlier output would be redirected into pair[] too
                     * and the first read() would not contain "redirected". */
    dup2(pair[1], 1);
    printf("redirected");              /* must land in the pipe, not console */
    fflush(stdout);
    dup2(saved, 1); close(saved); close(pair[1]);
    char rb[32] = {0};
    r = read(pair[0], rb, sizeof(rb) - 1);
    CHECK(r > 0 && strstr(rb, "redirected") != NULL, "dup2 redirection");
    close(pair[0]);

    /* 4. writev / readv */
    int pp[2]; pipe(pp);
    struct iovec wv[2] = {
        { .iov_base = (void *)"AB", .iov_len = 2 },
        { .iov_base = (void *)"CD", .iov_len = 2 },
    };
    r = writev(pp[1], wv, 2);
    CHECK(r == 4, "writev");
    struct iovec rv[2] = {
        { .iov_base = buf, .iov_len = 2 },
        { .iov_base = buf + 2, .iov_len = 2 },
    };
    r = readv(pp[0], rv, 2);
    CHECK(r == 4 && memcmp(buf, "ABCD", 4) == 0, "readv");
    close(pp[0]); close(pp[1]);

    /* 5. getdents64 via readdir(): / must contain at least usr and home */
    DIR *d = opendir("/");
    if (d) {
        int found_usr = 0, found_home = 0, n = 0;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            n++;
            if (!strcmp(e->d_name, "usr")) found_usr = 1;
            if (!strcmp(e->d_name, "home")) found_home = 1;
        }
        closedir(d);
        CHECK(n > 0 && found_usr && found_home, "readdir over / (getdents64)");
    } else {
        CHECK(0, "opendir /");
    }

    /* 6. unlink of nonexistent file returns ENOENT (fs is writable now) */
    int u = unlink("/tmp/nope");
    CHECK(u == -1 && errno == 2, "unlink nonexistent returns ENOENT");

    printf(fails ? "abitest: %d FAILURES\n" : "abitest: ALL PASS\n", fails);
    return fails ? 1 : 0;
}
