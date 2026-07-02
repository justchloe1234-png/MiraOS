#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    const uint8_t *data;
} font_glyph_t;

typedef struct {
    const char *name;
    uint32_t default_width;
    uint32_t default_height;
    font_glyph_t glyphs[96];
} font_t;

void text_init(void);
void text_draw(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);
void text_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void text_draw_scaled(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg, uint32_t scale);
void text_draw_char_scaled(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg, uint32_t scale);
void text_draw_wrapped(uint32_t x, uint32_t y, uint32_t max_width, const char *str, uint32_t fg, uint32_t bg);
size_t text_width(const char *str);
size_t text_width_char(char c);
size_t text_height(void);
void text_set_font(const font_t *font);
const font_t *text_get_font(void);