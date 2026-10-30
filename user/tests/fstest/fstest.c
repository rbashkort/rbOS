/* fstest: exercise writable filesystems.
 * 1) ramfs (/tmp): create via O_CREAT, write, read back, O_TRUNC, unlink
 * 2) mkdir/rmdir
 * 3) procfs reads
 * 4) sysfs reads */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static int fails = 0;
#define CHECK(cond, name) do { \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s (%s)\n", name, strerror(errno)); fails++; } \
} while (0)

int main(int argc, char *argv[]) {
    /* persist mode: verify ext2 data survived a reboot */
    if (argc > 1 && argv[1][0] == 'p') {
        int fd = open("/home/ext2test.txt", O_RDONLY);
        if (fd < 0) { printf("persist: FAIL open\n"); return 1; }
        char pb[64] = {0};
        read(fd, pb, sizeof(pb));
        close(fd);
        printf("persist: read '%s' -> %s\n", pb,
            strcmp(pb, "persistent ext2 data v1") == 0 ? "MATCH" : "MISMATCH");
        return strcmp(pb, "persistent ext2 data v1") != 0;
    }

    /* --- ramfs file lifecycle --- */
    int fd = open("/tmp/fstest.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    CHECK(fd >= 0, "open O_CREAT|O_WRONLY|O_TRUNC");
    const char *msg = "hello from ramfs";
    ssize_t w = write(fd, msg, strlen(msg));
    CHECK(w == (ssize_t)strlen(msg), "write");
    close(fd);

    fd = open("/tmp/fstest.txt", O_RDONLY);
    CHECK(fd >= 0, "reopen O_RDONLY");
    char buf[64] = {0};
    ssize_t r = read(fd, buf, sizeof(buf));
    CHECK(r == (ssize_t)strlen(msg) && strcmp(buf, msg) == 0, "read-back matches");
    struct stat st;
    CHECK(fstat(fd, &st) == 0 && st.st_size == (off_t)strlen(msg), "fstat size");
    close(fd);

    /* O_EXCL must fail now */
    errno = 0;
    int fd2 = open("/tmp/fstest.txt", O_CREAT | O_EXCL | O_WRONLY);
    CHECK(fd2 < 0 && errno == EEXIST, "O_EXCL fails on existing");
    if (fd2 >= 0) close(fd2);

    /* truncate */
    fd = open("/tmp/fstest.txt", O_WRONLY | O_TRUNC);
    CHECK(fd >= 0, "reopen with O_TRUNC");
    if (fstat(fd, &st) != 0 || st.st_size != 0) {
        printf("  [dbg] fstat rc=%zd size=%lld\n", (ssize_t)fstat(fd, &st), (long long)st.st_size);
    }
    CHECK(fstat(fd, &st) == 0 && st.st_size == 0, "O_TRUNC zeroed size");
    close(fd);

    CHECK(unlink("/tmp/fstest.txt") == 0, "unlink");
    CHECK(open("/tmp/fstest.txt", O_RDONLY) < 0 && errno == ENOENT, "gone after unlink");

    /* --- directories --- */
    CHECK(mkdir("/tmp/dir1", 0755) == 0, "mkdir /tmp/dir1");
    fd = open("/tmp/dir1/inner", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0, "create inside new dir");
    write(fd, "x", 1);
    close(fd);

    errno = 0;
    CHECK(rmdir("/tmp/dir1") == -1 && errno == ENOTEMPTY, "rmdir non-empty fails");

    CHECK(unlink("/tmp/dir1/inner") == 0, "unlink inner");
    CHECK(rmdir("/tmp/dir1") == 0, "rmdir empty dir");

    /* --- procfs --- */
    errno = 0; /* CHECK prints strerror(errno) on failure: clear the stale
                * ENOTEMPTY left by the rmdir test above so a procfs FAIL
                * does not blame the wrong syscall. */
    fd = open("/proc/version", O_RDONLY);
    CHECK(fd >= 0, "open /proc/version");
    memset(buf, 0, sizeof(buf));
    r = read(fd, buf, sizeof(buf) - 1);
    CHECK(r > 0 && strstr(buf, "rbKernel") != NULL, "proc version content");
    close(fd);

    fd = open("/proc/self/cmdline", O_RDONLY);
    CHECK(fd >= 0, "open /proc/self/cmdline");
    memset(buf, 0, sizeof(buf));
    r = read(fd, buf, sizeof(buf) - 1);
    CHECK(r > 0 && strstr(buf, "fstest"), "cmdline contains argv0");
    close(fd);

    fd = open("/proc/meminfo", O_RDONLY);
    CHECK(fd >= 0 && read(fd, buf, sizeof(buf) - 1) > 0, "meminfo readable");
    close(fd);

    /* P8.4: there is no Linux /proc/hwinfo; fetch assembles its own view. */
    errno = 0;
    fd = open("/proc/hwinfo", O_RDONLY);
    CHECK(fd < 0 && errno == ENOENT, "/proc/hwinfo is absent");
    if (fd >= 0) close(fd);

    /* pid dir of a real process */
    fd = open("/proc/1/cmdline", O_RDONLY);
    CHECK(fd >= 0, "open /proc/1/cmdline");
    close(fd);

    /* --- sysfs --- */
    fd = open("/sys/version", O_RDONLY);
    CHECK(fd >= 0 && read(fd, buf, sizeof(buf) - 1) > 0, "/sys/version");
    close(fd);

    /* --- ext2 write path (persisted across reboots) --- */
    /* Clean slate: previous runs may have left these on disk. */
    unlink("/home/ext2test.txt");
    unlink("/home/ext2big.bin");
    fd = open("/home/ext2test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    CHECK(fd >= 0, "ext2: open O_CREAT");
    const char *emsg = "persistent ext2 data v1";
    CHECK(write(fd, emsg, strlen(emsg)) == (ssize_t)strlen(emsg), "ext2: write");
    close(fd);

    fd = open("/home/ext2test.txt", O_RDONLY);
    memset(buf, 0, sizeof(buf));
    r = read(fd, buf, sizeof(buf));
    CHECK(r == (ssize_t)strlen(emsg) && strcmp(buf, emsg) == 0, "ext2: read-back");
    fstat(fd, &st);
    CHECK(st.st_size == (off_t)strlen(emsg), "ext2: size correct");
    close(fd);

    /* append beyond first block (indirect allocation) */
    fd = open("/home/ext2big.bin", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        char big[3000];
        memset(big, 'A', sizeof(big));
        ssize_t bw = write(fd, big, sizeof(big));
        CHECK(bw == (ssize_t)sizeof(big), "ext2: 3KB write crosses blocks");
        close(fd);
        fd = open("/home/ext2big.bin", O_RDONLY);
        int all_a = 1;
        for (int i = 0; i < 3000; i++) { char c; if (read(fd,&c,1)!=1 || c!='A') {all_a=0;break;} }
        CHECK(all_a, "ext2: 3KB verify");
        close(fd);
    }

    /* mkdir on ext2 + nested file */
    CHECK(mkdir("/home/dir2", 0755) == 0, "ext2: mkdir");
    fd = open("/home/dir2/nested.txt", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0, "ext2: create in new dir");
    write(fd, "nested", 6);
    close(fd);
    errno = 0;
    CHECK(rmdir("/home/dir2") == -1 && errno == ENOTEMPTY, "ext2: rmdir non-empty fails");
    CHECK(unlink("/home/dir2/nested.txt") == 0, "ext2: unlink nested");
    CHECK(rmdir("/home/dir2") == 0, "ext2: rmdir empty");

    printf(fails ? "fstest: %d FAILURES\n" : "fstest: ALL PASS\n", fails);
    return fails ? 1 : 0;
}
