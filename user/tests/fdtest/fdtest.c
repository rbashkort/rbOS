/* fdtest — P5 measurement + regression for the shared fd/node pools.
 *
 * Before P5 the per-process fd table was already 1024 entries (MAX_FDS) but
 * the global pools were MAX_FILE_DESCS=512 and VFS_MAX_NODES=512, so a
 * process could not actually reach its RLIMIT_NOFILE soft limit: open()
 * failed with EMFILE from the pools first. This test opens TARGET_OPEN
 * files at once (600 > 512) and also probes how many extra 4 KiB anonymous
 * VMAs fit in the VMA table (measurement only; P5.2 moved it to a heap Vec
 * with a 1024-entry cap, so the probe should now reach ~1000).
 *
 * P5.3: the fd table itself is now a sparse heap Vec too. The high-fd block
 * below grows it with F_DUPFD and dup2, reads through fd 1000, toggles
 * FD_CLOEXEC there (and checks dup3's O_CLOEXEC, BUG-015), and verifies that
 * close() frees the slot for reuse.
 *
 * Prints FDTEST-OK on success, FDTEST-FAIL: <msg> on a failed assertion.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#define TARGET_OPEN 600
#define VMA_PROBE_MAX 1200

static int g_fails;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FDTEST-FAIL: %s (errno=%d)\n", (msg), errno);              \
            g_fails++;                                                         \
            errno = 0;                                                         \
        }                                                                      \
    } while (0)

static void report_rlimit(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
        printf("FDTEST: rlim_nofile cur=%llu max=%llu\n",
               (unsigned long long)rl.rlim_cur,
               (unsigned long long)rl.rlim_max);
    else
        printf("FDTEST: getrlimit failed errno=%d\n", errno);
}

int main(void)
{
    report_rlimit();

    static int fds[TARGET_OPEN];
    for (int i = 0; i < TARGET_OPEN; i++)
        fds[i] = -1;

    int opened = 0;
    for (int i = 0; i < TARGET_OPEN; i++) {
        int fd = open("/home/test.txt", O_RDONLY);
        if (fd < 0) {
            printf("FDTEST: open stopped at %d errno=%d\n", i, errno);
            break;
        }
        fds[i] = fd;
        opened++;
    }
    printf("FDTEST: opened=%d target=%d\n", opened, TARGET_OPEN);
    CHECK(opened == TARGET_OPEN, "open(TARGET_OPEN) reached the soft limit");

    for (int i = 0; i < opened; i++)
        close(fds[i]);

    /* P5.3: sparse fd table (heap Vec). Sequential opens must stay
     * contiguous, a closed hole must be reused first, and the table must
     * grow for F_DUPFD/dup2 targets far beyond the low slots. */
    int a = open("/home/test.txt", O_RDONLY);
    int b = open("/home/test.txt", O_RDONLY);
    CHECK(a >= 0 && b == a + 1, "sequential opens get contiguous fds");
    if (a >= 0) {
        close(a);
        int reused = open("/home/test.txt", O_RDONLY);
        CHECK(reused == a, "lowest free fd reused after close");
        if (reused >= 0)
            close(reused);
    }
    if (b >= 0)
        close(b);

    int base = open("/home/test.txt", O_RDONLY);
    CHECK(base >= 0, "high-fd: base open");
    if (base >= 0) {
        int d = fcntl(base, F_DUPFD, 900);
        CHECK(d >= 900 && d < 1024, "F_DUPFD grows the table past 900");
        CHECK(dup2(base, 1000) == 1000, "dup2 grows the table to fd 1000");
        CHECK(fcntl(1000, F_SETFD, FD_CLOEXEC) == 0,
              "F_SETFD on fd 1000");
        CHECK((fcntl(1000, F_GETFD) & FD_CLOEXEC) != 0,
              "F_GETFD on fd 1000");
        char ch = 0;
        CHECK(lseek(1000, 0, SEEK_SET) == 0, "lseek fd 1000");
        CHECK(read(1000, &ch, 1) == 1, "read via fd 1000");
        close(1000);
        if (d >= 0)
            close(d);
        int d2 = fcntl(base, F_DUPFD, 900);
        CHECK(d2 >= 900 && d2 < 1024, "F_DUPFD reuses the freed high slot");
        if (d2 >= 0)
            close(d2);
        /* BUG-015: dup3's O_CLOEXEC must reach the kernel (the dispatch
         * checked 0o20000000 instead of 0o2000000, so the flag was lost). */
        int e3 = dup3(base, 1001, O_CLOEXEC);
        CHECK(e3 == 1001, "dup3 to fd 1001");
        if (e3 >= 0) {
            CHECK((fcntl(1001, F_GETFD) & FD_CLOEXEC) != 0,
                  "dup3(O_CLOEXEC) sets FD_CLOEXEC");
            close(1001);
        }
        close(base);
    }

    /* Measurement only: how many additional 4 KiB anonymous VMAs fit. */
    void *vmas[VMA_PROBE_MAX];
    int n_vma = 0;
    for (int i = 0; i < VMA_PROBE_MAX; i++) {
        void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            printf("FDTEST: mmap stopped at %d errno=%d\n", i, errno);
            break;
        }
        vmas[n_vma++] = p;
    }
    printf("FDTEST: vma_extra=%d\n", n_vma);
    /* Hard check that P5.2's raised cap is real: the old 128-slot table
     * stopped this probe at ~110, so anything below 900 means a regression. */
    CHECK(n_vma >= 900, "VMA probe reached the raised cap (P5.2)");
    for (int i = 0; i < n_vma; i++)
        munmap(vmas[i], 4096);

    /* The cap failure must be transactional: once the probe VMAs are
     * unmapped the freed holes must be reusable, so one more mmap has to
     * succeed. This catches a half-committed insert (slot consumed but no
     * region recorded) as well as a table left permanently full. */
    void *again = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(again != MAP_FAILED, "mmap after VMA-cap failure + munmap");
    if (again != MAP_FAILED)
        munmap(again, 4096);

    if (g_fails == 0)
        printf("FDTEST-OK\n");
    return g_fails ? 1 : 0;
}
