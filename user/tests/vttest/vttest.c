// vttest: exercises the virtual-console switch ioctls (VT_GETSTATE,
// VT_ACTIVATE). Run from tty1: it queries the active VT, switches to tty2,
// prints there, sleeps (so the tester can screenshot tty2), and switches
// back. All output also lands on the serial log via the tty serial mirror.
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#define VT_GETSTATE 0x5603
#define VT_ACTIVATE 0x5606
#define VT_WAITACTIVE 0x5607

struct vt_stat {
    unsigned short v_state;
    unsigned short v_active;
    unsigned short v_signal;
};

int main(void) {
    int fd = open("/dev/tty0", O_RDWR);
    if (fd < 0) {
        printf("vttest: open /dev/tty0 failed\n");
        return 1;
    }
    struct vt_stat st = { 0, 0, 0 };
    if (ioctl(fd, VT_GETSTATE, &st) != 0) {
        printf("vttest: VT_GETSTATE failed\n");
        return 1;
    }
    printf("vttest: state=0x%x active=%d\n", st.v_state, st.v_active);
    if (st.v_active != 1) {
        printf("vttest: expected active=1\n");
        return 1;
    }
    printf("vttest: switching to tty2\n");
    fflush(stdout);
    if (ioctl(fd, VT_ACTIVATE, (void *)2) != 0) {
        printf("vttest: VT_ACTIVATE 2 failed\n");
        return 1;
    }
    printf("vttest: hello from tty2\n");
    printf("vttest: press enter\n");
    fflush(stdout);
    // Block until the tester sends Enter (stdin follows the active VT,
    // so this read is served from tty2 while we are switched to it).
    {
        char buf[16];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        printf("vttest: got %d bytes\n", (int)n);
    }
    if (ioctl(fd, VT_ACTIVATE, (void *)1) != 0) {
        printf("vttest: VT_ACTIVATE 1 failed\n");
        return 1;
    }
    printf("vttest: back on tty1\n");
    fflush(stdout);

    // VT_WAITACTIVE: the child must stay parked until tty2 is activated.
    {
        pid_t c = fork();
        if (c < 0) {
            printf("vttest: fork failed\n");
            return 1;
        }
        if (c == 0) {
            if (ioctl(fd, VT_WAITACTIVE, (void *)2) != 0)
                _exit(1);
            _exit(0);
        }
        // The child must still be blocked after a busy window (active==1).
        int spins, alive = 1, status = 0;
        for (spins = 0; spins < 200000; spins++) {
            if (waitpid(c, &status, WNOHANG) != 0) {
                alive = 0;
                break;
            }
        }
        if (!alive) {
            printf("vttest: WAITACTIVE returned early\n");
            return 1;
        }
        if (ioctl(fd, VT_ACTIVATE, (void *)2) != 0) {
            printf("vttest: VT_ACTIVATE 2 (wake) failed\n");
            return 1;
        }
        if (waitpid(c, &status, 0) != c || !WIFEXITED(status)
            || WEXITSTATUS(status) != 0) {
            printf("vttest: WAITACTIVE child failed\n");
            ioctl(fd, VT_ACTIVATE, (void *)1);
            return 1;
        }
        printf("vttest: waitactive ok\n");
        if (ioctl(fd, VT_ACTIVATE, (void *)1) != 0) {
            printf("vttest: VT_ACTIVATE 1 (restore) failed\n");
            return 1;
        }
    }
    printf("vttest: back on tty1 PASS\n");
    return 0;
}
