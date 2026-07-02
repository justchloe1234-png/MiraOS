#include "framebuffer.h"
#include "driver.h"
#include "kernel.h"
#include "kernel/panic.h"
#include "kernel/heap.h"
#include "lib/common/mem.h"

static framebuffer_t fb;
static uint32_t *back_buffer = 0;
static bool use_double_buffer = false;

static struct multiboot_tag *mbi_next(struct multiboot_tag *tag) {
    return (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
}

static int fb_driver_init(void) {
    return fb_ready() ? 0 : -1;
}

static driver_t fb_driver = {
    .name = "fb",
    .id = DRIVER_ID_FB,
    .init = fb_driver_init,
    .irq = 0,
    .next = 0
};

DRIVER_REGISTER(fb_driver);

bool fb_init(uint64_t mbi) {
    struct multiboot_tag *tag = (struct multiboot_tag *)(mbi + 8);
    while (tag->type != 0) {
        if (tag->type == 8) {
            struct multiboot_tag_framebuffer *fbt = (void *)tag;
            fb.addr = fbt->framebuffer_addr;
            fb.pitch = fbt->framebuffer_pitch;
            fb.width = fbt->framebuffer_width;
            fb.height = fbt->framebuffer_height;
            fb.bpp = fbt->framebuffer_bpp;
            
            /* Allocate back buffer for double buffering */
            size_t buf_size = fb.height * fb.pitch;
            back_buffer = kmalloc(buf_size);
            if (back_buffer) {
                use_double_buffer = true;
            }
            
            return fb.bpp == 32;
        }
        tag = mbi_next(tag);
    }
    return false;
}

bool fb_ready(void) {
    return fb.addr != 0 && fb.bpp == 32;
}

framebuffer_t *fb_info(void) {
    return &fb;
}

static inline void fb_write_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_ready() || x >= fb.width || y >= fb.height)
        return;
    
    uint32_t *pixel;
    if (use_double_buffer) {
        pixel = &back_buffer[y * (fb.pitch / 4) + x];
    } else {
        pixel = (uint32_t *)(fb.addr + y * fb.pitch + x * 4);
    }
    *pixel = color;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    fb_write_pixel(x, y, color);
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb_ready())
        return;
    
    /* Clip to screen bounds */
    if (x >= fb.width || y >= fb.height)
        return;
    if (x + w > fb.width)
        w = fb.width - x;
    if (y + h > fb.height)
        h = fb.height - y;
    
    for (uint32_t row = y; row < y + h; row++) {
        uint32_t *line;
        if (use_double_buffer) {
            line = &back_buffer[row * (fb.pitch / 4) + x];
        } else {
            line = (uint32_t *)(fb.addr + row * fb.pitch + x * 4);
        }
        for (uint32_t col = 0; col < w; col++)
            line[col] = color;
    }
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    fb_fill_rect(x, y, w, h, color);
}

void fb_draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t thickness) {
    for (uint32_t t = 0; t < thickness; t++) {
        fb_fill_rect(x + t, y + t, w - 2 * t, 1, color);
        fb_fill_rect(x + t, y + h - 1 - t, w - 2 * t, 1, color);
        fb_fill_rect(x + t, y + t, 1, h - 2 * t, color);
        fb_fill_rect(x + w - 1 - t, y + t, 1, h - 2 * t, color);
    }
}

void fb_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color) {
    /* Bresenham's line algorithm */
    int dx = abs((int)x1 - (int)x0);
    int dy = abs((int)y1 - (int)y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        fb_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void fb_draw_circle(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color, bool filled) {
    if (!fb_ready())
        return;
    
    int x = radius;
    int y = 0;
    int err = 0;
    
    while (x >= y) {
        if (filled) {
            fb_fill_rect(cx - x, cy - y, 2 * x + 1, 1, color);
            fb_fill_rect(cx - x, cy + y, 2 * x + 1, 1, color);
            fb_fill_rect(cx - y, cy - x, 2 * y + 1, 1, color);
            fb_fill_rect(cx - y, cy + x, 2 * y + 1, 1, color);
        } else {
            fb_put_pixel(cx + x, cy + y, color);
            fb_put_pixel(cx + y, cy + x, color);
            fb_put_pixel(cx - y, cy + x, color);
            fb_put_pixel(cx - x, cy + y, color);
            fb_put_pixel(cx - x, cy - y, color);
            fb_put_pixel(cx - y, cy - x, color);
            fb_put_pixel(cx + y, cy - x, color);
            fb_put_pixel(cx + x, cy - y, color);
        }
        
        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void fb_blit(uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, const uint32_t *src, uint32_t src_pitch) {
    if (!fb_ready() || !src)
        return;
    
    for (uint32_t row = 0; row < h && dy + row < fb.height; row++) {
        if (dx + w > fb.width)
            w = fb.width - dx;
        
        uint32_t *dst_line;
        if (use_double_buffer) {
            dst_line = &back_buffer[(dy + row) * (fb.pitch / 4) + dx];
        } else {
            dst_line = (uint32_t *)(fb.addr + (dy + row) * fb.pitch + dx * 4);
        }
        const uint32_t *src_line = (const uint32_t *)((const uint8_t *)src + row * src_pitch);
        for (uint32_t col = 0; col < w; col++)
            dst_line[col] = src_line[col];
    }
}

void fb_scroll(uint32_t lines) {
    if (!fb_ready() || lines == 0)
        return;
    
    size_t row_bytes = fb.pitch;
    size_t total_bytes = fb.height * fb.pitch;
    size_t scroll_bytes = lines * row_bytes;
    
    if (use_double_buffer) {
        memmove(back_buffer, (uint8_t *)back_buffer + scroll_bytes, total_bytes - scroll_bytes);
        memset((uint8_t *)back_buffer + total_bytes - scroll_bytes, 0, scroll_bytes);
    } else {
        memmove((void *)fb.addr, (void *)(fb.addr + scroll_bytes), total_bytes - scroll_bytes);
        memset((void *)(fb.addr + total_bytes - scroll_bytes), 0, scroll_bytes);
    }
}

void fb_swap_buffers(void) {
    if (!use_double_buffer || !back_buffer)
        return;
    
    size_t total_bytes = fb.height * fb.pitch;
    memcpy((void *)fb.addr, back_buffer, total_bytes);
}

void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, fb.width, fb.height, color);
}

void fb_shutdown(void) {
    if (back_buffer) {
        kfree(back_buffer);
        back_buffer = 0;
    }
    use_double_buffer = false;
    fb.addr = 0;
    fb.width = 0;
    fb.height = 0;
    fb.bpp = 0;
}