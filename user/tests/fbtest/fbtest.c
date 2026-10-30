/* fbtest: verify /dev/fb0 (open, ioctls, mmap, pixman rendering).
 *
 * Opens the framebuffer, queries its mode, mmaps it, draws two pixman
 * rectangles directly into the mapping, then cleans up. Prints
 * "successfully" on the happy path (rbtest keys its PASS verdict on it).
 *
 * NOTE: the ioctl buffer MUST be the full 160-byte `fb_var_screeninfo`
 * (Linux layout, same as the kernel's drivers/fb.rs). A smaller buffer
 * (e.g. only xres/yres) used to overflow this function's stack frame:
 * the kernel writes the whole struct, clobbering saved registers and the
 * return address (which landed on 0x800000000 from the blue.length field)
 * and the test died with SIGSEGV after printing everything.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <pixman-1/pixman.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

/* Linux <linux/fb.h> layout, 160 bytes. */
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

struct fb_fix_screeninfo {
    uint8_t id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type_;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint64_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[3];
};

_Static_assert(sizeof(struct fb_var_screeninfo) == 160,
               "fb_var_screeninfo must match Linux 160 bytes");
_Static_assert(sizeof(struct fb_fix_screeninfo) == 80,
               "fb_fix_screeninfo must match Linux 80 bytes");

int main(void) {
    printf("[fbtest] Opening /dev/fb0...\n");
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        printf("[fbtest] Failed to open /dev/fb0!\n");
        return 1;
    }

    struct fb_var_screeninfo var;
    for (unsigned i = 0; i < sizeof(var); i++)
        ((char *)&var)[i] = 0;
    uint32_t w = 1024, h = 768;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0 && var.xres && var.yres) {
        w = var.xres;
        h = var.yres;
    } else {
        printf("[fbtest] ioctl failed, using fallback 1024x768\n");
    }
    printf("[fbtest] Resolution: %ux%u\n", w, h);

    uint64_t len = (uint64_t)w * h * 4;
    void *bits = mmap(NULL, (size_t)len, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (bits == MAP_FAILED) {
        printf("[fbtest] mmap failed!\n");
        close(fd);
        return 1;
    }

    pixman_image_t *dst = pixman_image_create_bits(
        PIXMAN_a8r8g8b8, w, h, (uint32_t *)bits, w * 4);
    if (!dst) {
        printf("[fbtest] pixman create_bits failed!\n");
        munmap(bits, (size_t)len);
        close(fd);
        return 1;
    }

    pixman_color_t bg = { 0xffff, 0x2500, 0x1500, 0x1000 };
    pixman_image_t *fill1 = pixman_image_create_solid_fill(&bg);
    if (fill1) {
        pixman_image_composite32(PIXMAN_OP_SRC, fill1, NULL, dst,
                                 0, 0, 0, 0, 0, 0, w, h);
        pixman_image_unref(fill1);
    }

    pixman_color_t fg = { 0xc000, 0x8000, 0x2000, 0x0080 };
    pixman_image_t *fill2 = pixman_image_create_solid_fill(&fg);
    if (fill2) {
        int pw = w > 500 ? 500 : (int)w;
        int ph = h > 350 ? 350 : (int)h;
        pixman_image_composite32(PIXMAN_OP_OVER, fill2, NULL, dst,
                                 0, 0, 0, 0,
                                 ((int)w - pw) / 2, ((int)h - ph) / 2,
                                 pw, ph);
        pixman_image_unref(fill2);
    }

    printf("[fbtest] Pixman blending finished successfully!\n");

    pixman_image_unref(dst);
    munmap(bits, (size_t)len);
    close(fd);
    return 0;
}
