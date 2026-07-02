#pragma once

#include <stdint.h>
#include <stdbool.h>

void gfx_init(void);
void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gfx_draw_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color, uint32_t thickness);
void gfx_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color);
void gfx_draw_circle(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t color, bool filled);
void gfx_blit(uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, const uint32_t *src, uint32_t src_pitch);
void gfx_scroll(uint32_t lines);
void gfx_swap_buffers(void);
void gfx_clear(uint32_t color);
