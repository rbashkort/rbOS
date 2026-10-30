#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

typedef enum {
    MODE_CONSOLE,
    MODE_GRAPHICS
} rbit_mode_t;

static rbit_mode_t current_mode = MODE_CONSOLE;
static pid_t shell_pid = -1;
static pid_t wm_pid = -1;

static char *default_envp[] = {
    "PATH=/usr/bin:/bin",
    "TERM=vt100",
    "HOME=/home",
    "USER=root",
    NULL
};

static pid_t spawn_shell(void) {
    pid_t pid = fork();

    if (pid < 0) {
        printf("[rbit] fork failed for shell!\n");
        return -1;
    }

    if (pid == 0) {
        char *argv[] = { "sh", "-i", NULL };
        execve("/bin/sh", argv, default_envp);
        printf("[rbit] failed to exec /bin/sh\n");
        exit(1);
    }

    printf("[rbit] Started shell (PID %d)\n", pid);
    return pid;
}

static pid_t spawn_wm(void) {
    pid_t pid = fork();

    if (pid < 0) {
        printf("[rbit] fork failed for WM!\n");
        return -1;
    }

    if (pid == 0) {
        char *argv[] = { "rbwm", NULL };
        execve("/usr/bin/rbwm", argv, default_envp);
        printf("[rbit] failed to exec /usr/bin/rbwm, falling back to shell\n");
        exit(1);
    }

    printf("[rbit] Started Window Manager (PID %d)\n", pid);
    return pid;
}

int main(void) {
    printf("\n--- rbOS System Initialization (rbit PID 1) ---\n\n");

    shell_pid = spawn_shell();

    while (1) {
        int status;
        pid_t reaped = waitpid(-1, &status, 0);

        if (reaped > 0) {
            printf("\n[rbit] Process %d exited (status %d)\n", reaped, WEXITSTATUS(status));

            if (reaped == shell_pid) {
                shell_pid = -1;

                if (current_mode == MODE_CONSOLE) {
                    sleep(1);
                    shell_pid = spawn_shell();
                }
            }
            else if (reaped == wm_pid) {
                wm_pid = -1;
                printf("[rbit] WM exited, returning to console mode\n");
                current_mode = MODE_CONSOLE;
                sleep(1);
                shell_pid = spawn_shell();
            }
        } else if (reaped < 0) {
            if (errno == EINTR) continue;
            sleep(1);
        }
    }

    return 0;
}
