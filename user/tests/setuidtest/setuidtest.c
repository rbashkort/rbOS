#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define FAIL(fmt, ...) do { \
    printf("FAIL: " fmt "\n", ##__VA_ARGS__); \
    return 1; \
} while (0)

int main(void) {
    const char *prv = "/home/prv.txt";
    const char *pub = "/home/pub.txt";

    printf("setuidtest: start\n");

    /* Ensure files exist. If absent as root, create them with fixed modes. */
    int fd = open(prv, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) return 0; /* already there or not needed */
    close(fd);
    int pfd = open(pub, O_CREAT | O_WRONLY, 0644);
    if (pfd >= 0) close(pfd);

    int start = geteuid();
    printf("setuidtest: euid before = %d\n", start);
    if (start != 0)
        FAIL("not started as root (euid=%d)", start);

    /* As root, we may open a 0600 root-owned file. */
    fd = open(prv, O_RDONLY);
    if (fd < 0)
        FAIL("root could not open %s: errno=%d", prv, errno);
    close(fd);
    printf("setuidtest: root opened %s OK\n", prv);

    /* Drop privileges. */
    if (setuid(1000) != 0)
        FAIL("setuid(1000) failed: errno=%d", errno);
    int euid = geteuid();
    printf("setuidtest: euid after setuid(1000) = %d\n", euid);
    if (euid != 1000)
        FAIL("euid after setuid != 1000 (got %d)", euid);

    /* Non-root must NOT open a 0600 root-owned file -> EACCES. */
    errno = 0;
    fd = open(prv, O_RDONLY);
    if (fd >= 0)
        FAIL("uid1000 opened root 0600 file (should be denied)");
    if (errno != EACCES)
        FAIL("expected EACCES (%d), got errno=%d", EACCES, errno);
    printf("setuidtest: uid1000 got EACCES on %s OK\n", prv);

    /* Non-root MAY open a 0644 world-readable file. */
    fd = open(pub, O_RDONLY);
    if (fd < 0)
        FAIL("uid1000 could not open %s: errno=%d", pub, errno);
    close(fd);
    printf("setuidtest: uid1000 opened %s OK\n", pub);

    /* Writing to a root-only file must also fail. */
    errno = 0;
    fd = open(prv, O_WRONLY);
    if (fd >= 0)
        FAIL("uid1000 opened %s for write (should be denied)", prv);
    if (errno != EACCES)
        FAIL("write: expected EACCES (%d), got errno=%d", EACCES, errno);

    printf("setuidtest: ALL PASS\n");
    return 0;
}
