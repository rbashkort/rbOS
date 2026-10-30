#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
    char buf[1024];
    ssize_t n;

    if (argc == 1) {
        while ((n = read(0, buf, sizeof(buf))) > 0) {
            if (write(1, buf, n) != n) return 1;
        }
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        int fd;
        if (argv[i][0] == '-' && argv[i][1] == '\0') {
            fd = 0;
        } else {
            fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "cat: %s: cannot open\n", argv[i]);
                continue;
            }
        }
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            if (write(1, buf, n) != n) return 1;
        }
        if (fd != 0) close(fd);
    }
    return 0;
}
