#include "gfx.h"
#include "drivers/framebuffer.h"

void gfx_init(void) {
}

void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    fb_draw_rect(x, y, w, h, color);
}

void gfx_draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t thickness) {
    fb_draw_rect_outline(x, y, w, h, color, thickness);
}

void gfx_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color) {
    fb_draw_line(x0, y0, x1, y1, color);
}

void gfx_draw_circle(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color, bool filled) {
    fb_draw_circle(cx, cy, radius, color, filled);
}

void gfx_blit(uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, const uint32_t *src, uint32_t src_pitch) {
    fb_blit(dx, dy, w, h, src, src_pitch);
}

void gfx_scroll(uint32_t lines) {
    fb_scroll(lines);
}

void gfx_swap_buffers(void) {
    fb_swap_buffers();
}

void gfx_clear(uint32_t color) {
    fb_clear(color);
}