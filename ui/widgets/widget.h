#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} ui_rect_t;

typedef struct {
    ui_rect_t bounds;
    uint32_t fill;
    uint32_t border;
    uint32_t title_bg;
    const char *title;
} ui_panel_t;

typedef struct {
    ui_rect_t bounds;
    uint32_t fill;
    uint32_t border;
    uint32_t text_color;
    const char *label;
    bool pressed;
} ui_button_t;

typedef struct {
    ui_rect_t bounds;
    uint32_t fill;
    uint32_t border;
    uint32_t text_color;
    char *buffer;
    size_t buflen;
    size_t len;
    bool focused;
} ui_textfield_t;

void ui_panel_draw(const ui_panel_t *panel);
void ui_button_draw(const ui_button_t *btn);
void ui_textfield_draw(const ui_textfield_t *field);
bool ui_rect_contains(const ui_rect_t *r, uint32_t px, uint32_t py);
