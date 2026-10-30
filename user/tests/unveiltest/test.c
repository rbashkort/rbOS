/* P7.d: unveil(2) positive, negative, inheritance and locking tests.
 *
 * The test is a dynamic rbOS binary.  It sets up its fixtures before the first
 * successful unveil, then restricts itself to /tmp, /home, /usr/bin and
 * /usr/lib for the rest of the run.  Negative cases fork so a failure is
 * reported by the parent instead of aborting the test.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define HELPER "/usr/bin/unveil-helper"

static void fail(const char *what, int err) {
    printf("UNVEIL-FAIL: %s (errno=%d)\n", what, err);
    _exit(1);
}

static void check(int cond, const char *what, int err) {
    if (!cond)
        fail(what, err);
}

static int wait_child(pid_t pid, const char *what) {
    int st = 0;
    if (waitpid(pid, &st, 0) != pid)
        fail(what, errno);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("UNVEIL-FAIL: %s (status=0x%x)\n", what, st);
        return -1;
    }
    return 0;
}

static void write_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        fail(path, errno);
    if (write(fd, "x", 1) != 1)
        fail("write fixture", errno);
    close(fd);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    /* ---- before the first successful unveil: everything is visible ---- */
    int fd = open("/home/test.txt", O_RDONLY);
    check(fd >= 0, "baseline open", errno);
    close(fd);

    /* Malformed calls must fail without activating the sandbox. */
    errno = 0;
    check(unveil("/tmp", "rwz") == -1 && errno == EINVAL,
          "bad permission accepted", errno);
    errno = 0;
    check(unveil(NULL, "r") == -1 && errno == EINVAL,
          "NULL path accepted", errno);
    errno = 0;
    check(unveil("/tmp", NULL) == -1 && errno == EINVAL,
          "NULL permissions accepted", errno);
    errno = 0;
    check(unveil("", "r") == -1 && errno == EINVAL,
          "empty path accepted", errno);
    fd = open("/home/test.txt", O_RDONLY);
    check(fd >= 0, "failed unveil activated sandbox", errno);
    close(fd);
    printf("[ok] malformed unveil calls are EINVAL and inactive\n");

    /* A pledge without the "unveil" promise disables future unveil calls;
     * OpenBSD reports EPERM here rather than a fatal pledge violation. */
    pid_t pid = fork();
    if (pid < 0)
        fail("fork pledge-lock probe", errno);
    if (pid == 0) {
        if (pledge("stdio", NULL) != 0)
            _exit(5);
        errno = 0;
        if (unveil("/tmp", "r") != -1 || errno != EPERM)
            _exit(6);
        _exit(0);
    }
    if (wait_child(pid, "pledge locks unveil") != 0)
        return 1;
    printf("[ok] pledge without unveil locks unveil with EPERM\n");

    /* With the promise, the same call is allowed. */
    pid = fork();
    if (pid < 0)
        fail("fork pledge-unveil probe", errno);
    if (pid == 0) {
        if (pledge("stdio unveil", NULL) != 0)
            _exit(7);
        if (unveil("/tmp", "r") != 0)
            _exit(8);
        _exit(0);
    }
    if (wait_child(pid, "pledge allows unveil") != 0)
        return 1;
    printf("[ok] pledge with unveil permits unveil\n");

    /* ---- fixtures, while the filesystem is still unrestricted ---- */
    mkdir("/tmp/uv", 0700);
    mkdir("/tmp/uv/dir", 0700);
    mkdir("/tmp/uv/empty", 0700);
    unlink("/tmp/uv/recreate/f"); /* repeat runs on the same disk image */
    rmdir("/tmp/uv/recreate");
    mkdir("/tmp/uv/recreate", 0700);
    write_file("/tmp/uv/rw");
    write_file("/tmp/uv/dir/ro");
    write_file("/tmp/uv/recreate/f");
    unlink("/tmp/uv/new");

    /* First successful call activates the list.  The order matters only for
     * readability: /tmp is broad, then the narrower dir/empty rules. */
    check(unveil("/home", "r") == 0, "unveil /home", errno);
    check(unveil("/tmp", "rwc") == 0, "unveil /tmp", errno);
    check(unveil("/sbin", "rx") == 0, "unveil /sbin", errno);
    printf("[ok] initial unveil list\n");

    struct stat st;

    /* ---- symlink escape: /sbin/init -> /usr/bin/rbit, while /usr/bin is
     * still hidden. A lexical check would accept /sbin/init; the VFS walk
     * must resolve the target and answer ENOENT. The link itself is visible
     * through lstat/readlink because those do not follow the final element. */
    errno = 0;
    check(open("/sbin/init", O_RDONLY) == -1 && errno == ENOENT,
          "symlink target hidden", errno);
    errno = 0;
    check(stat("/sbin/init", &st) == -1 && errno == ENOENT,
          "symlink stat target hidden", errno);
    check(lstat("/sbin/init", &st) == 0 && S_ISLNK(st.st_mode),
          "nofollow lstat of link", errno);
    char linkbuf[64];
    ssize_t linklen = readlink("/sbin/init", linkbuf, sizeof(linkbuf));
    check(linklen > 0, "readlink link", errno);
    printf("[ok] symlink target is checked after expansion\n");

    check(unveil("/usr/bin", "rwxc") == 0, "unveil /usr/bin", errno);
    check(unveil("/usr/lib", "r") == 0, "unveil /usr/lib", errno);
    fd = open("/sbin/init", O_RDONLY);
    check(fd >= 0, "symlink target after unveil", errno);
    close(fd);

    /* ---- parent symlink expansion: /bin -> usr/bin. The policy entry is
     * /usr/bin, so create and rename through /bin must be allowed there and
     * must not be judged by the hidden-looking /bin spelling. */
    fd = open("/bin/unveil-created", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    check(fd >= 0, "create through symlink parent", errno);
    close(fd);
    check(unlink("/bin/unveil-created") == 0,
          "unlink through symlink parent", errno);
    write_file("/usr/bin/unveil-rename-src");
    check(rename("/usr/bin/unveil-rename-src", "/bin/unveil-renamed") == 0,
          "rename through symlink parent", errno);
    check(stat("/usr/bin/unveil-renamed", &st) == 0,
          "renamed destination visible", errno);
    check(unlink("/bin/unveil-renamed") == 0,
          "cleanup renamed destination", errno);
    printf("[ok] create/rename through a symlinked parent\n");

    /* ---- directory identity: a directory remembered by unveil() must not
     * silently name a different object after remove/recreate. A helper is
     * exec'd so the replacement happens outside the parent's unveil list. */
    check(unveil("/tmp/uv/recreate", "rwc") == 0,
          "unveil recreate dir", errno);
    errno = 0;
    check(mkdir("/tmp/uv/recreate", 0700) == -1 && errno == EEXIST,
          "mkdir existing unveiled dir", errno);
    pid = fork();
    if (pid < 0)
        fail("fork recreate helper", errno);
    if (pid == 0) {
        char *args[] = {"/usr/bin/unveil-helper", "recreate",
                        "/tmp/uv/recreate", NULL};
        char *envp[] = {NULL};
        execve(args[0], args, envp);
        _exit(9);
    }
    if (wait_child(pid, "recreate helper") != 0)
        return 1;
    errno = 0;
    check(stat("/tmp/uv/recreate", &st) == -1 && errno == ENOENT,
          "recreated dir stale stat", errno);
    errno = 0;
    check(open("/tmp/uv/recreate/f", O_RDONLY) == -1 && errno == ENOENT,
          "recreated dir stale open", errno);
    check(unveil("/tmp/uv/recreate", "rwc") == 0,
          "re-unveil recreated dir", errno);
    check(stat("/tmp/uv/recreate", &st) == 0,
          "re-unveiled recreated dir", errno);
    printf("[ok] directory identity is remembered across remove/recreate\n");

    /* Visible and readable. */
    fd = open("/home/test.txt", O_RDONLY);
    check(fd >= 0, "visible read", errno);
    close(fd);
    check(stat("/home/test.txt", &st) == 0, "visible stat", errno);

    /* Hidden paths report ENOENT, not EACCES: existence is not leaked. */
    errno = 0;
    check(open("/etc/shells", O_RDONLY) == -1 && errno == ENOENT,
          "hidden open", errno);
    errno = 0;
    check(stat("/etc/shells", &st) == -1 && errno == ENOENT,
          "hidden stat", errno);
    errno = 0;
    check(access("/etc/shells", F_OK) == -1 && errno == ENOENT,
          "hidden access", errno);
    printf("[ok] hidden paths are ENOENT\n");

    /* Mount-point operations must not leak existence as EBUSY/ENOTDIR when
     * the mount path is outside the unveil list. */
    errno = 0;
    check(mkdir("/dev", 0700) == -1 && errno == ENOENT,
          "hidden mount mkdir", errno);
    errno = 0;
    check(unlink("/dev") == -1 && errno == ENOENT,
          "hidden mount unlink", errno);
    errno = 0;
    check(rename("/dev", "/tmp/dev") == -1 && errno == ENOENT,
          "hidden mount rename", errno);
    printf("[ok] hidden mount point reports ENOENT\n");

    /* Longest component-boundary prefix wins: /tmp is rwc, but the narrower
     * /tmp/uv/dir entry only grants r.  A later call cannot widen it back. */
    check(unveil("/tmp/uv/dir", "r") == 0, "unveil narrow dir", errno);
    fd = open("/tmp/uv/dir/ro", O_RDONLY);
    check(fd >= 0, "narrow read", errno);
    close(fd);
    errno = 0;
    check(open("/tmp/uv/dir/ro", O_WRONLY) == -1 && errno == EACCES,
          "narrow write", errno);
    check(unveil("/tmp/uv/dir", "rw") == 0, "unveil widen attempt", errno);
    errno = 0;
    check(open("/tmp/uv/dir/ro", O_WRONLY) == -1 && errno == EACCES,
          "unveil widened", errno);
    printf("[ok] longest prefix and monotonic narrowing\n");

    /* An empty permission entry is visible (F_OK) but not accessible. */
    check(unveil("/tmp/uv/empty", "") == 0, "unveil empty perms", errno);
    check(access("/tmp/uv/empty", F_OK) == 0, "empty F_OK", errno);
    errno = 0;
    check(stat("/tmp/uv/empty", &st) == -1 && errno == EACCES,
          "empty stat", errno);
    printf("[ok] empty permissions are visible but inaccessible\n");

    /* /tmp keeps c/r/w, so creation under it still works. */
    fd = open("/tmp/uv/new", O_WRONLY | O_CREAT | O_EXCL, 0600);
    check(fd >= 0, "create under /tmp", errno);
    close(fd);
    printf("[ok] c/r/w creation under /tmp\n");

    /* ---- lock: NULL,NULL succeeds once, then every call is EPERM ---- */
    check(unveil(NULL, NULL) == 0, "unveil lock", errno);
    errno = 0;
    check(unveil(NULL, NULL) == -1 && errno == EPERM,
          "second lock", errno);
    errno = 0;
    check(unveil("/", "r") == -1 && errno == EPERM,
          "unveil after lock", errno);
    printf("[ok] unveil(NULL,NULL) locks the list\n");

    /* ---- fork copies the list and the lock ---- */
    pid = fork();
    if (pid < 0)
        fail("fork", errno);
    if (pid == 0) {
        int cfd = open("/home/test.txt", O_RDONLY);
        if (cfd < 0)
            _exit(1);
        close(cfd);
        errno = 0;
        if (open("/etc/shells", O_RDONLY) != -1 || errno != ENOENT)
            _exit(2);
        errno = 0;
        if (open("/tmp/uv/dir/ro", O_WRONLY) != -1 || errno != EACCES)
            _exit(3);
        errno = 0;
        if (unveil("/", "r") != -1 || errno != EPERM)
            _exit(4);
        _exit(0);
    }
    if (wait_child(pid, "fork inheritance") != 0)
        return 1;
    printf("[ok] fork copies entries and the lock\n");

    /* ---- exec clears the list: the new program starts unrestricted ---- */
    pid = fork();
    if (pid < 0)
        fail("fork", errno);
    if (pid == 0) {
        char *argv[] = { (char *)HELPER, NULL };
        char *envp[] = { NULL };
        execve(HELPER, argv, envp);
        _exit(127);
    }
    if (wait_child(pid, "exec clears unveil") != 0)
        return 1;
    printf("[ok] exec clears unveil\n");

    printf("UNVEIL-OK\n");
    return 0;
}
