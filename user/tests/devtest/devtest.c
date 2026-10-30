/* devtest: verify standard devices in /dev */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main(void) {
    char buf[64];

    /* null: write accepts everything, read returns EOF */
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0) { printf("FAIL: open(/dev/null): %m\n"); return 1; }
    ssize_t w = write(fd, "hello", 5);
    ssize_t r = read(fd, buf, sizeof(buf));
    printf("null: write=%zd read=%zd %s\n", w, r, (w == 5 && r == 0) ? "OK" : "FAIL");
    close(fd);

    /* zero: reads return zeros */
    fd = open("/dev/zero", O_RDONLY);
    if (fd < 0) { printf("FAIL: open(/dev/zero)\n"); return 1; }
    memset(buf, 0xAA, sizeof(buf));
    r = read(fd, buf, 16);
    int zeros = 1;
    for (int i = 0; i < 16; i++) if (buf[i] != 0) zeros = 0;
    printf("zero: read=%zd %s\n", r, (r == 16 && zeros) ? "OK" : "FAIL");
    close(fd);

    /* urandom: two reads must differ */
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) { printf("FAIL: open(/dev/urandom)\n"); return 1; }
    unsigned long a = 0, b = 0;
    r = read(fd, &a, sizeof(a));
    r += read(fd, &b, sizeof(b));
    printf("urandom: read=%zd %s (%08lx %08lx)\n", r,
           (r == 16 && a != b) ? "OK" : "FAIL", a, b);
    close(fd);

    return 0;
}
