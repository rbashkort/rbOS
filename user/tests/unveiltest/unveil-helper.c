/* P7.d exec probe: a static no-loader binary.
 *
 * Default mode: the parent has unveiled /home and /usr/bin and locked the
 * list.  A successful exec must clear the parent's unveils, so both the
 * allowed and the previously hidden file open.  If the list survived exec,
 * the hidden open would fail with ENOENT and this helper exits 1.
 *
 * "recreate <dir>" mode: run outside the parent's unveil list (exec clears
 * it) and remove/recreate a directory, so the parent can verify that the
 * remembered directory identity goes stale.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int recreate(const char *dir) {
    char file[512];
    snprintf(file, sizeof(file), "%s/f", dir);
    if (unlink(file) != 0 && errno != ENOENT) {
        printf("UNVEIL-HELPER-FAIL: recreate unlink errno=%d\n", errno);
        return 1;
    }
    if (rmdir(dir) != 0 && errno != ENOENT) {
        printf("UNVEIL-HELPER-FAIL: recreate rmdir errno=%d\n", errno);
        return 1;
    }
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
        printf("UNVEIL-HELPER-FAIL: recreate mkdir errno=%d\n", errno);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "recreate") == 0) {
        if (recreate(argv[2]) != 0)
            return 1;
        printf("UNVEIL-HELPER-RECREATE-OK\n");
        return 0;
    }

    int fd = open("/home/test.txt", O_RDONLY);
    if (fd < 0) {
        printf("UNVEIL-HELPER-FAIL: allowed open errno=%d\n", errno);
        return 1;
    }
    close(fd);

    fd = open("/etc/shells", O_RDONLY);
    if (fd < 0) {
        printf("UNVEIL-HELPER-FAIL: post-exec open errno=%d\n", errno);
        return 1;
    }
    close(fd);

    printf("UNVEIL-HELPER-OK\n");
    return 0;
}
