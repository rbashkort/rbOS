#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <pixman-1/pixman.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

struct fb_bitfield {
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

/* Linux <linux/fb.h> layout, 160 bytes. Must match the kernel's
 * drivers/fb.rs FbVarScreeninfo: sys_ioctl writes the whole struct, so a
 * shorter buffer would overflow this frame (the old 68-byte version here
 * survived only because main() never returns). */
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

/* Linux <linux/fb.h> layout, 80 bytes (note: no padding after smem_len —
 * the old version had a bogus _pad shifting every later field by 4). */
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

typedef struct {
    uint8_t flags;
    int8_t  dx;
    int8_t  dy;
} mouse_packet_t;

typedef struct {
    int x, y, w, h;
    pixman_color_t color;
    int is_dragging;
    int drag_offset_x, drag_offset_y;
} window_t;

static void draw_cursor(pixman_image_t* dest, int cx, int cy) {
    static const uint8_t cursor_bitmap[12][8] = {
        {1,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0},
        {1,2,2,1,0,0,0,0},
        {1,2,2,2,1,0,0,0},
        {1,2,2,2,2,1,0,0},
        {1,2,2,2,2,2,1,0},
        {1,2,2,1,1,1,1,0},
        {1,1,1,2,1,0,0,0},
        {1,0,0,1,2,1,0,0},
        {0,0,0,0,1,1,0,0},
        {0,0,0,0,0,0,0,0}
    };

    pixman_color_t black = { 0, 0, 0, 0xffff };
    pixman_color_t white = { 0xffff, 0xffff, 0xffff, 0xffff };
    pixman_image_t* img_black = pixman_image_create_solid_fill(&black);
    pixman_image_t* img_white = pixman_image_create_solid_fill(&white);

    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t p = cursor_bitmap[y][x];
            if (p == 1) {
                pixman_image_composite(PIXMAN_OP_SRC, img_black, NULL, dest, 0, 0, 0, 0, cx + x, cy + y, 1, 1);
            } else if (p == 2) {
                pixman_image_composite(PIXMAN_OP_SRC, img_white, NULL, dest, 0, 0, 0, 0, cx + x, cy + y, 1, 1);
            }
        }
    }

    pixman_image_unref(img_black);
    pixman_image_unref(img_white);
}

int main(void) {
    printf("[rbwm]: Starting Window Manager\n");

    int fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        printf("[rbwm]: Failed to open /dev/fb0!\n");
        return 1;
    }

    int mouse_fd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
    if (mouse_fd < 0) {
        printf("[rbwm]: Warning: Failed to open /dev/mouse!\n");
    }

    struct fb_var_screeninfo var = {0};
    struct fb_fix_screeninfo fix = {0};

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0) {
        var.xres = 1024; var.yres = 768;
        var.bits_per_pixel = 32;
    }
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
        fix.line_length = 0;
    }

    uint32_t width = var.xres, height = var.yres;
    uint32_t bpp = var.bits_per_pixel ? var.bits_per_pixel : 32;
    uint32_t stride = fix.line_length ? fix.line_length : ((width * bpp + 7) / 8);
    size_t fb_size = stride * height;

    printf("[rbwm]: fb %ux%u pitch=%u bpp=%u size=%u\n",
           width, height, stride, var.bits_per_pixel, (unsigned)fb_size);

    uint32_t* fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        printf("[rbwm]: ERROR mmap(/dev/fb0) failed\n");
        return 1;
    }
    uint32_t* backbuffer = (uint32_t*)malloc(fb_size);
    if (!backbuffer) {
        printf("[rbwm]: ERROR malloc(%u) failed\n", (unsigned)fb_size);
        return 1;
    }
    printf("[rbwm]: fb_mem=%#lx backbuffer=%p\n", (unsigned long)fb_mem, backbuffer);

    pixman_image_t* bb_img = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, backbuffer, stride);
    if (!bb_img) {
        printf("[rbwm]: ERROR pixman image create failed\n");
        return 1;
    }
    pixman_color_t bg_color = { 0x1e1e, 0x1e1e, 0x2e2e, 0xffff };
    pixman_image_t* bg_src = pixman_image_create_solid_fill(&bg_color);

    window_t win = {
        .x = 200, .y = 150, .w = 400, .h = 250,
        .color = { 0x8888, 0xc0c0, 0xd0d0, 0xffff },
        .is_dragging = 0
    };

    int cursor_x = width / 2;
    int cursor_y = height / 2;

    printf("[rbwm]: Loop started!\n");
    while (1) {
        if (mouse_fd >= 0) {
            mouse_packet_t pkt;
            int bytes = read(mouse_fd, &pkt, sizeof(pkt));
            //if (bytes <= 0) { printf("[rbwm]: skibidi\n"); }
            if (bytes == sizeof(mouse_packet_t)) {
                cursor_x += pkt.dx;
                cursor_y -= pkt.dy;

                if (cursor_x < 0) cursor_x = 0;
                if (cursor_y < 0) cursor_y = 0;
                if (cursor_x >= (int)width)  cursor_x = width - 1;
                if (cursor_y >= (int)height) cursor_y = height - 1;

                int left_click = (pkt.flags & 1) != 0;

                if (left_click) {
                    if (!win.is_dragging) {
                        if (cursor_x >= win.x && cursor_x <= win.x + win.w &&
                            cursor_y >= win.y && cursor_y <= win.y + win.h) {
                            win.is_dragging = 1;
                            win.drag_offset_x = cursor_x - win.x;
                            win.drag_offset_y = cursor_y - win.y;
                        }
                    } else {
                        win.x = cursor_x - win.drag_offset_x;
                        win.y = cursor_y - win.drag_offset_y;
                    }
                } else {
                    win.is_dragging = 0;
                }
            }
            //else { printf("[rbwm]: toilet\n"); }
        }

        pixman_image_composite(PIXMAN_OP_SRC, bg_src, NULL, bb_img, 0, 0, 0, 0, 0, 0, width, height);
        pixman_image_t* win_src = pixman_image_create_solid_fill(&win.color);
        pixman_image_composite(PIXMAN_OP_SRC, win_src, NULL, bb_img, 0, 0, 0, 0, win.x, win.y, win.w, win.h);
        pixman_image_unref(win_src);

        draw_cursor(bb_img, cursor_x, cursor_y);

        memcpy(fb_mem, backbuffer, fb_size);
        usleep(10000);
    }
    return 0;
}
