/* fetch: fastfetch-style system summary.
 *
 * This is deliberately a userspace-side assembly of standard interfaces:
 * uname(2), /proc/{cpuinfo,meminfo,uptime}, /etc/os-release,
 * /dev/fb0 ioctls and the raw /sys/block/ata0/device/model attribute.
 * The kernel must not grow a pre-formatted /proc/hwinfo for this tool.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#define C       "\033[36m"
#define BOLD    "\033[1m"
#define R       "\033[0m"
#define GUTTER_W 16

#define FBIOGET_VSCREENINFO 0x4600

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

/* Linux <linux/fb.h> layout, 160 bytes.  The kernel ioctl writes the whole
 * structure, so a shorter local copy would smash the stack. */
struct fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
};

_Static_assert(sizeof(struct fb_var_screeninfo) == 160,
               "fb_var_screeninfo must match Linux 160 bytes");

static const char *logo[] = {
    "               ",
    " _ __   _      ",
    "| '__| | |__   ",
    "| |    | '_ \\  ",
    "|_|    | |_) |",
    "       |_.__/ ",
};
#define LOGO_ROWS ((int)(sizeof(logo) / sizeof(logo[0])))

static int read_file(const char *path, char *out, size_t outsz) {
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t n = fread(out, 1, outsz - 1, f);
    fclose(f);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        n--;
    out[n] = 0;
    return (int)n;
}

static char *trim(char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
    return s;
}

/* Read "Key: value" or "Key\t: value" from a proc-style text file. */
static int read_key(const char *path, const char *key, char *out, size_t outsz) {
    static char buf[8192];
    if (read_file(path, buf, sizeof(buf)) < 0)
        return 0;

    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl)
            *nl = 0;
        char *colon = strchr(p, ':');
        if (colon) {
            *colon = 0;
            if (strcmp(trim(p), key) == 0) {
                snprintf(out, outsz, "%s", trim(colon + 1));
                return 1;
            }
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    return 0;
}

static void unquote(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
        memmove(s, s + 1, n - 2);
        s[n - 2] = 0;
    }
}

static void read_os_release(char *out, size_t outsz) {
    static char buf[4096];
    char name[128] = "";
    char version[64] = "";

    snprintf(out, outsz, "rbOS 0.1");
    if (read_file("/etc/os-release", buf, sizeof(buf)) < 0)
        return;

    char *p = buf;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl)
            *nl = 0;
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = 0;
            char *k = trim(p);
            char *v = trim(eq + 1);
            unquote(v);
            if (strcmp(k, "PRETTY_NAME") == 0) {
                snprintf(out, outsz, "%s", v);
                return;
            }
            if (strcmp(k, "NAME") == 0)
                snprintf(name, sizeof(name), "%s", v);
            else if (strcmp(k, "VERSION") == 0)
                snprintf(version, sizeof(version), "%s", v);
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    if (name[0] && version[0])
        snprintf(out, outsz, "%s %s", name, version);
    else if (name[0])
        snprintf(out, outsz, "%s", name);
}

static void read_screen(char *out, size_t outsz) {
    int fd = open("/dev/fb0", O_RDONLY);
    if (fd < 0)
        return;

    struct fb_var_screeninfo var;
    memset(&var, 0, sizeof(var));
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 &&
        var.xres != 0 && var.yres != 0) {
        snprintf(out, outsz, "%ux%u @ %u bpp",
                 var.xres, var.yres, var.bits_per_pixel);
    }
    close(fd);
}

int main(void) {
    char os[256] = "rbOS 0.1";
    char kernel[256] = "rbKernel";
    char arch[64] = "x86_64";
    char cpu[256] = "";
    char cpu_brand[192] = "";
    char cpu_vendor[96] = "";
    char siblings[32] = "";
    char cache[32] = "";
    char uptime_buf[64] = "";
    int have_uptime = 0;
    char mem_total[32] = "";
    char mem_free[32] = "";
    char disk[128] = "";
    char screen[64] = "";
    unsigned long long total_kb = 0, free_kb = 0, used_kb = 0;
    unsigned long long uptime_secs = 0;
    unsigned long long l2_kb = 0;

    read_os_release(os, sizeof(os));

    struct utsname u;
    if (uname(&u) == 0) {
        snprintf(kernel, sizeof(kernel), "%s %s", u.sysname, u.release);
        snprintf(arch, sizeof(arch), "%s", u.machine);
    }

    if (read_key("/proc/cpuinfo", "model name", cpu_brand, sizeof(cpu_brand)) == 0)
        read_key("/proc/cpuinfo", "vendor_id", cpu_vendor, sizeof(cpu_vendor));
    read_key("/proc/cpuinfo", "siblings", siblings, sizeof(siblings));
    if (read_key("/proc/cpuinfo", "cache size", cache, sizeof(cache)))
        l2_kb = strtoull(cache, NULL, 10);
    if (cpu_brand[0]) {
        snprintf(cpu, sizeof(cpu), "%s", cpu_brand);
    } else if (cpu_vendor[0]) {
        snprintf(cpu, sizeof(cpu), "%s", cpu_vendor);
    } else {
        snprintf(cpu, sizeof(cpu), "unknown CPU");
    }
    if (siblings[0]) {
        size_t used = strlen(cpu);
        snprintf(cpu + used, sizeof(cpu) - used, " (%lu threads)",
                 strtoul(siblings, NULL, 10));
    }
    if (read_file("/proc/uptime", uptime_buf, sizeof(uptime_buf)) > 0) {
        uptime_secs = strtoull(uptime_buf, NULL, 10);
        have_uptime = 1;
    }

    if (read_key("/proc/meminfo", "MemTotal", mem_total, sizeof(mem_total)))
        total_kb = strtoull(mem_total, NULL, 10);
    if (read_key("/proc/meminfo", "MemFree", mem_free, sizeof(mem_free)))
        free_kb = strtoull(mem_free, NULL, 10);
    used_kb = total_kb > free_kb ? total_kb - free_kb : 0;

    if (read_file("/sys/block/ata0/device/model", disk, sizeof(disk)) > 0) {
        char *d = trim(disk);
        if (strcmp(d, "unknown") == 0 || d[0] == 0)
            disk[0] = 0;
        else
            memmove(disk, d, strlen(d) + 1);
    } else {
        disk[0] = 0;
    }

    read_screen(screen, sizeof(screen));

    char lines[12][256];
    int nfields = 0;
    snprintf(lines[nfields++], sizeof(lines[0]), "OS:         %s (%s %s)",
             os, kernel, arch);
    snprintf(lines[nfields++], sizeof(lines[0]), "Kernel:     %s", kernel);
    if (have_uptime) {
        snprintf(lines[nfields++], sizeof(lines[0]),
                 "Uptime:     %llu mins, %llu secs",
                 uptime_secs / 60, uptime_secs % 60);
    }
    snprintf(lines[nfields++], sizeof(lines[0]), "CPU:        %s", cpu);
    if (l2_kb != 0) {
        snprintf(lines[nfields++], sizeof(lines[0]), "Cache:      L2 %llu KiB", l2_kb);
    }
    if (disk[0]) {
        snprintf(lines[nfields++], sizeof(lines[0]), "Disk(ata0): %s", disk);
    }
    if (screen[0]) {
        snprintf(lines[nfields++], sizeof(lines[0]), "Screen:     %s", screen);
    }
    if (total_kb != 0) {
        snprintf(lines[nfields++], sizeof(lines[0]),
                 "Memory:     %llu MiB / %llu MiB",
                 used_kb / 1024, total_kb / 1024);
    }

    char gutter[GUTTER_W + 1];
    for (int i = 0; i < LOGO_ROWS || i < nfields + 2; i++) {
        const char *art = (i < LOGO_ROWS) ? logo[i] : "";
        snprintf(gutter, sizeof(gutter), "%-*s", GUTTER_W, art);

        /* Reset BEFORE the newline so colors never leak across lines. */
        if (i == 0)
            printf(C "%s  " BOLD "root@rbOS" R "\n", gutter);
        else if (i == 1)
            printf(C "%s" R "  ----------------\n", gutter);
        else {
            int k = i - 2;
            printf(C "%s  " R "%s\n", gutter, k < nfields ? lines[k] : "");
        }
    }
    return 0;
}
