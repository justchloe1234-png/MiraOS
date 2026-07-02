#include "window.h"
#include "gfx.h"
#include "text.h"
#include "drivers/framebuffer.h"

static ui_window_t windows[MAX_WINDOWS];
static uint32_t num_windows;
static uint32_t focused_window;
static int32_t drag_window_idx;

void window_init(void) {
    num_windows = 0;
    focused_window = 0;
    drag_window_idx = -1;
}

void window_create(const char *title, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (num_windows >= MAX_WINDOWS)
        return;
    
    uint32_t idx = num_windows++;
    windows[idx].bounds.x = x;
    windows[idx].bounds.y = y;
    windows[idx].bounds.w = w;
    windows[idx].bounds.h = h;
    windows[idx].fill = 0x16213E;
    windows[idx].border = 0x533483;
    windows[idx].title_bg = 0x0F3460;
    windows[idx].title = title;
    windows[idx].visible = true;
    windows[idx].focused = false;
    windows[idx].dragging = false;
    windows[idx].drag_offset_x = 0;
    windows[idx].drag_offset_y = 0;
    
    window_bring_to_front(idx);
}

void window_close(uint32_t index) {
    if (index >= num_windows)
        return;
    
    for (uint32_t i = index; i < num_windows - 1; i++) {
        windows[i] = windows[i + 1];
    }
    num_windows--;
    
    if (focused_window >= num_windows)
        focused_window = num_windows > 0 ? num_windows - 1 : 0;
}

void window_bring_to_front(uint32_t index) {
    if (index >= num_windows)
        return;
    
    for (uint32_t i = 0; i < num_windows; i++) {
        windows[i].focused = false;
    }
    
    ui_window_t temp = windows[index];
    for (uint32_t i = index; i > 0; i--) {
        windows[i] = windows[i - 1];
    }
    windows[0] = temp;
    windows[0].focused = true;
    focused_window = 0;
}

void window_render(void) {
    if (!fb_ready())
        return;
    
    for (uint32_t i = 0; i < num_windows; i++) {
        ui_window_t *win = &windows[i];
        if (!win->visible)
            continue;
        
        gfx_draw_rect(win->bounds.x, win->bounds.y, win->bounds.w, win->bounds.h, win->fill);
        gfx_draw_rect_outline(win->bounds.x, win->bounds.y, win->bounds.w, win->bounds.h, win->border, 2);
        
        if (win->title) {
            gfx_draw_rect(win->bounds.x + 2, win->bounds.y + 2, win->bounds.w - 4, 24, win->title_bg);
            text_draw(win->bounds.x + 10, win->bounds.y + 6, win->title, 0xFFFFFF, win->title_bg);
        }
    }
}

bool window_handle_click(uint32_t x, uint32_t y) {
    for (int i = (int)num_windows - 1; i >= 0; i--) {
        ui_window_t *win = &windows[i];
        if (!win->visible)
            continue;
        
        if (x >= win->bounds.x && x < win->bounds.x + win->bounds.w &&
            y >= win->bounds.y && y < win->bounds.y + win->bounds.h) {
            
            window_bring_to_front(i);
            /* After bring_to_front the window is now at index 0 */
            if (y < windows[0].bounds.y + 26) {
                windows[0].dragging = true;
                windows[0].drag_offset_x = (int32_t)x - (int32_t)windows[0].bounds.x;
                windows[0].drag_offset_y = (int32_t)y - (int32_t)windows[0].bounds.y;
                drag_window_idx = 0;
            }
            
            return true;
        }
    }
    return false;
}

bool window_handle_drag(uint32_t x, uint32_t y) {
    if (drag_window_idx < 0 || drag_window_idx >= (int)num_windows)
        return false;
    
    ui_window_t *win = &windows[drag_window_idx];
    if (!win->dragging)
        return false;
    
    win->bounds.x = x - win->drag_offset_x;
    win->bounds.y = y - win->drag_offset_y;
    return true;
}

void window_set_focused(uint32_t index) {
    if (index < num_windows)
        window_bring_to_front(index);
}

uint32_t window_count(void) {
    return num_windows;
}
