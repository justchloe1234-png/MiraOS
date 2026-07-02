#include "widget.h"
#include "gfx.h"
#include "text.h"

bool ui_rect_contains(const ui_rect_t *r, uint32_t px, uint32_t py) {
    return px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h;
}

void ui_panel_draw(const ui_panel_t *panel) {
    gfx_draw_rect(panel->bounds.x, panel->bounds.y, panel->bounds.w, panel->bounds.h, panel->fill);
    gfx_draw_rect_outline(panel->bounds.x, panel->bounds.y, panel->bounds.w, panel->bounds.h, panel->border, 2);
    if (panel->title) {
        gfx_draw_rect(panel->bounds.x + 2, panel->bounds.y + 2, panel->bounds.w - 4, 22, panel->title_bg);
        text_draw(panel->bounds.x + 10, panel->bounds.y + 6, panel->title, 0xFFFFFF, panel->title_bg);
    }
}

void ui_button_draw(const ui_button_t *btn) {
    uint32_t fill = btn->pressed ? 0x0F3460 : btn->fill;
    gfx_draw_rect(btn->bounds.x, btn->bounds.y, btn->bounds.w, btn->bounds.h, fill);
    gfx_draw_rect_outline(btn->bounds.x, btn->bounds.y, btn->bounds.w, btn->bounds.h, btn->border, 2);
    if (btn->label) {
        uint32_t tx = btn->bounds.x + 12;
        uint32_t ty = btn->bounds.y + (btn->bounds.h > 16 ? (btn->bounds.h - 16) / 2 : 4);
        text_draw(tx, ty, btn->label, btn->text_color, fill);
    }
}

void ui_textfield_draw(const ui_textfield_t *field) {
    uint32_t fill = field->fill;
    uint32_t border = field->focused ? 0x53D8FB : field->border;
    gfx_draw_rect(field->bounds.x, field->bounds.y, field->bounds.w, field->bounds.h, fill);
    gfx_draw_rect_outline(field->bounds.x, field->bounds.y, field->bounds.w, field->bounds.h, border, 2);
    if (field->buffer) {
        text_draw(field->bounds.x + 8, field->bounds.y + 8, field->buffer, field->text_color, fill);
    }
    if (field->focused) {
        uint32_t cx = field->bounds.x + 8 + (uint32_t)field->len * 8;
        gfx_draw_rect(cx, field->bounds.y + 6, 2, 16, field->text_color);
    }
}
