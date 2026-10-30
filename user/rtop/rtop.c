/* rtop: rbashkort's top. Per-process CPU/memory viewer sourced from /proc.
 *
 * Reads /proc/<pid>/stat (Linux field order), /proc/meminfo, /proc/uptime
 * and /proc/ticks_hz, samples the process table twice one second apart
 * and renders a top(1)-style table. Press Ctrl-C to quit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>

#define MAX_PROCS 1024
#define PROCLEN 16
#define LINE_SZ 512

struct proc {
    int pid;
    int ppid;
    char state;             /* R/S/Z */
    char comm[PROCLEN];     /* truncated to TASK_COMM_LEN (15) */
    unsigned long long utime; /* user CPU ticks */
    unsigned long long stime; /* kernel CPU ticks */
    long nthreads;
    unsigned long long vsize; /* virtual bytes */
    long rss;                 /* resident pages */
    unsigned long long starttime;
    unsigned cpu;             /* last CPU it ran on */
    int zombie;
};

static int hz = 250;            /* kernel tick rate (PIT) */
static unsigned long long page_kb = 4; /* 4096-byte pages, reported in kB */

static void on_sigint(int sig) {
    (void)sig;
    /* Restore the screen before dying. */
    printf("\033[2J\033[Hrtop: bye\n");
    _exit(0);
}

static int read_file(const char *path, char *buf, size_t sz) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, sz - 1, f);
    fclose(f);
    buf[n] = 0;
    return (int)n;
}

static unsigned long long parse_kb(const char *line) {
    /* "MemTotal:\t1024 kB" -> 1024 */
    const char *p = strchr(line, ':');
    if (!p) return 0;
    return strtoull(p + 1, NULL, 10);
}

/* Parse a /proc/<pid>/stat line into `p`. Returns 0 on success.
 * comm is inside parens and is the only place parens may appear, so
 * everything after the last ')' is a plain space-separated list starting
 * at field 3 (state). */
static int parse_stat(const char *path, struct proc *p) {
    static char line[LINE_SZ];
    if (read_file(path, line, sizeof(line)) <= 0) return -1;
    char *lp = strchr(line, '(');
    char *rp = strrchr(line, ')');
    if (!lp || !rp || rp < lp) return -1;
    p->pid = atoi(line);
    size_t clen = (size_t)(rp - lp - 1);
    if (clen > 15) clen = 15;
    memcpy(p->comm, lp + 1, clen);
    p->comm[clen] = 0;

    /* Field 3 (state) is a LETTER ("R"/"S"/"Z"...)  consume it first, then
     * fields 4.. are numbers: v[0]=ppid, v[10]=utime, v[47]=exit_code. */
    char *tok = rp + 1;
    while (*tok == ' ')
        tok++;
    if (!*tok)
        return -1;
    p->state = *tok;
    tok++;

    unsigned long long v[64];
    int n = 0;
    while (n < 64) {
        while (*tok == ' ')
            tok++;
        if (!*tok) break;
        char *end;
        v[n] = strtoull(tok, &end, 10);
        if (end == tok) break;
        n++;
        tok = end;
    }
    if (n < 48) return -1;      /* need fields 4..51 */
    p->ppid = (int)v[0];        /* field 4 */
    p->utime = v[10];           /* field 14 */
    p->stime = v[11];           /* field 15 */
    p->nthreads = (long)v[16];  /* field 20 */
    p->starttime = v[18];       /* field 22 */
    p->vsize = v[19];           /* field 23 */
    p->rss = (long)v[20];       /* field 24 */
    p->cpu = (unsigned)v[34];   /* field 38 */
    p->zombie = (p->state == 'Z');
    return 0;
}

static int is_num(const char *s) {
    if (!*s) return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9') return 0;
    return 1;
}

/* Snapshot the process table. Returns the number of processes collected. */
static int collect_procs(struct proc *out, int maxn) {
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) != NULL && n < maxn) {
        if (!is_num(e->d_name)) continue;
        char path[64];
        snprintf(path, sizeof(path), "/proc/%s/stat", e->d_name);
        struct proc tmp;
        if (parse_stat(path, &tmp) == 0)
            out[n++] = tmp;
    }
    closedir(d);
    return n;
}

static unsigned long long uptime_ms(void) {
    static char line[64];
    if (read_file("/proc/uptime", line, sizeof(line)) <= 0) return 0;
    unsigned long long s = strtoull(line, NULL, 10);
    const char *dot = strchr(line, '.');
    unsigned long long ms = 0;
    if (dot && dot[1] && dot[2])
        ms = (unsigned long long)(dot[1] - '0') * 100 + (unsigned long long)(dot[2] - '0') * 10;
    return s * 1000 + ms;
}

/* Total CPU time of a process, in seconds. */
static unsigned long long cpu_secs(const struct proc *p) {
    return (p->utime + p->stime) / (unsigned long long)hz;
}

/* CPU percent over the sampling window: delta cpu ticks / delta wall time. */
static double cpu_pct(const struct proc *cur, const struct proc *old,
                      unsigned long long wall_ms) {
    if (!old || wall_ms == 0) return 0.0;
    unsigned long long dt = (cur->utime + cur->stime) - (old->utime + old->stime);
    /* dt ticks per (wall_ms/1000) seconds of wall time; want % of one CPU. */
    return (double)dt * 100.0 * 1000.0 / ((double)hz * (double)wall_ms);
}

int main(void) {
    signal(SIGINT, on_sigint);
    static char line[LINE_SZ];

    if (read_file("/proc/ticks_hz", line, sizeof(line)) > 0)
        hz = atoi(line);
    if (hz <= 0) hz = 100;

    static char meminfo[1024];
    unsigned long long mem_total = 0, mem_avail = 0;
    if (read_file("/proc/meminfo", meminfo, sizeof(meminfo)) > 0) {
        char *save = NULL;
        for (char *l = strtok_r(meminfo, "\n", &save); l; l = strtok_r(NULL, "\n", &save)) {
            if (mem_total == 0 && strncmp(l, "MemTotal:", 9) == 0) mem_total = parse_kb(l);
            if (mem_avail == 0 && strncmp(l, "MemAvailable:", 13) == 0) mem_avail = parse_kb(l);
        }
    }

    for (;;) {
        static struct proc s1[MAX_PROCS], s2[MAX_PROCS];
        int n1 = collect_procs(s1, MAX_PROCS);
        unsigned long long t1 = uptime_ms();

        struct timespec req = {1, 0};
        nanosleep(&req, NULL);

        int n2 = collect_procs(s2, MAX_PROCS);
        unsigned long long t2 = uptime_ms();
        unsigned long long wall = t2 - t1;

        /* Clear and redraw. */
        printf("\033[2J\033[H");
        unsigned long long up = uptime_ms();
        printf("\033[7m rbOS rtop \033[0m  tasks: %d, mem: %lluMiB total, %lluMiB free, up %llumin %llus\n",
               n2,
               mem_total / 1024, mem_avail / 1024,
               up / 60000, (up / 1000) % 60);

        /* Bubble sort by %CPU desc (stable: keeps pid order for ties). */
        for (int i = 0; i < n2 - 1; i++) {
            for (int j = 0; j < n2 - 1 - i; j++) {
                struct proc *a = &s2[j], *b = &s2[j + 1];
                struct proc *oa = NULL, *ob = NULL;
                for (int k = 0; k < n1; k++) {
                    if (s1[k].pid == a->pid) oa = &s1[k];
                    if (s1[k].pid == b->pid) ob = &s1[k];
                }
                if (cpu_pct(a, oa, wall) < cpu_pct(b, ob, wall)) {
                    struct proc tmp = *a;
                    *a = *b;
                    *b = tmp;
                }
            }
        }

        printf("%5s %5s %3s %2s %6s %5s %8s %10s %8s %s\n",
               "PID", "PPID", "THR", "ST", "%CPU", "%MEM", "RSS(kB)", "VSIZE(kB)", "TIME", "COMMAND");

        for (int i = 0; i < n2; i++) {
            struct proc *p = &s2[i];
            struct proc *old = NULL;
            for (int k = 0; k < n1; k++)
                if (s1[k].pid == p->pid) { old = &s1[k]; break; }
            double pct = cpu_pct(p, old, wall);
            double mempct = mem_total
                                ? (double)p->rss * (double)page_kb * 1024.0 * 100.0 / ((double)mem_total * 1024.0)
                                : 0.0;
            unsigned long long tsec = cpu_secs(p);
            char st = p->zombie ? 'Z' : p->state;
            printf("%5d %5d %3ld %2c %6.1f %5.1f %8ld %10llu %02llu:%02llu %s\n",
                   p->pid, p->ppid, p->nthreads, st, pct, mempct,
                   (long)(p->rss * page_kb),
                   p->vsize / 1024,
                   tsec / 60, tsec % 60,
                   p->comm);
        }
        fflush(stdout);
    }
    return 0;
}
