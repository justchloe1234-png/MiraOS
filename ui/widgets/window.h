#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "widget.h"

#define MAX_WINDOWS 8

typedef struct {
    ui_rect_t bounds;
    uint32_t fill;
    uint32_t border;
    uint32_t title_bg;
    const char *title;
    bool visible;
    bool focused;
    bool dragging;
    int32_t drag_offset_x;
    int32_t drag_offset_y;
} ui_window_t;

void window_init(void);
void window_create(const char *title, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void window_close(uint32_t index);
void window_bring_to_front(uint32_t index);
void window_render(void);
bool window_handle_click(uint32_t x, uint32_t y);
bool window_handle_drag(uint32_t x, uint32_t y);
void window_set_focused(uint32_t index);
uint32_t window_count(void);