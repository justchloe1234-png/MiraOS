#include "safemode.h"
#include "kernel.h"
#include "kernel/mem.h"
#include "kernel/heap.h"
#include "arch/x86_64/cpu.h"
#include "drivers/framebuffer.h"
#include "drivers/timer.h"
#include "drivers/ps2/mouse.h"
#include "fs/vfs.h"
#include "ui/layout/text.h"
#include "ui/layout/gfx.h"
#include "lib/common/string.h"
#include "lib/common/mem.h"

/* Layout */
#define SM_BG           0x0A0A0A
#define SM_HEADER_BG    0x1A0000
#define SM_HEADER_FG    0xFF4444
#define SM_TEXT_FG      0xCCCCCC
#define SM_DIM_FG       0x666666
#define SM_GREEN_FG     0x44FF88

#define SM_BTN_BG           0x1E1E1E
#define SM_BTN_BG_HOVER     0x2A2A2A
#define SM_BTN_BORDER       0x444444
#define SM_BTN_BORDER_HOVER 0x888888
#define SM_BTN_TEXT         0xFFFFFF

#define SM_FONT_W       8
#define SM_FONT_H       16
#define SM_HEADER_H     48
#define SM_STATUS_H     24
#define SM_MAX_LINES    64
#define SM_LINE_LEN     256

#define SM_BTN_W        220
#define SM_BTN_H        48
#define SM_BTN_GAP      24

/* State */
static bool sm_active = false;
static char sm_reason[128];

/* Read-only diagnostics log, filled once on entry */
static char sm_lines[SM_MAX_LINES][SM_LINE_LEN];
static uint32_t sm_line_colors[SM_MAX_LINES];
static uint32_t sm_line_count = 0;

/* Buttons */
typedef struct {
    const char *label;
    uint32_t x, y, w, h;
    void (*action)(void);
} sm_button_t;

static void sm_action_restart(void);
static void sm_action_shutdown(void);

static sm_button_t sm_buttons[] = {
    { "Restart MiraOS",  0, 0, SM_BTN_W, SM_BTN_H, sm_action_restart },
    { "Shutdown MiraOS", 0, 0, SM_BTN_W, SM_BTN_H, sm_action_shutdown },
};
#define SM_BUTTON_COUNT (sizeof(sm_buttons) / sizeof(sm_buttons[0]))

/* Utility */
static void sm_str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int sm_str_len(const char *s) {
    int i = 0; while (s[i]) i++; return i;
}

static void sm_str_cat(char *dst, const char *src, int max) {
    int dlen = sm_str_len(dst);
    int i = 0;
    while (src[i] && dlen + i < max - 1) { dst[dlen + i] = src[i]; i++; }
    dst[dlen + i] = 0;
}

static void sm_uint_to_str(uint64_t v, char *buf) {
    if (v == 0) { buf[0]='0'; buf[1]=0; return; }
    char tmp[20]; int i=0;
    while (v) { tmp[i++]='0'+(v%10); v/=10; }
    int j=0;
    while(i>0) buf[j++]=tmp[--i];
    buf[j]=0;
}

static bool sm_point_in_rect(int32_t px, int32_t py, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    return px >= (int32_t)x && px < (int32_t)(x + w) &&
           py >= (int32_t)y && py < (int32_t)(y + h);
}

/* Print (diagnostics log only — not interactive) */
static void sm_print(const char *msg, uint32_t color) {
    if (sm_line_count >= SM_MAX_LINES)
        return;
    sm_str_copy(sm_lines[sm_line_count], msg, SM_LINE_LEN);
    sm_line_colors[sm_line_count] = color;
    sm_line_count++;
}

static void sm_collect_diagnostics(void) {
    char buf[128];

    sm_print("  System diagnostics", SM_GREEN_FG);

    sm_str_copy(buf, "  Uptime : ", 128);
    char tmp[24]; sm_uint_to_str(timer_ticks() / 100, tmp);
    sm_str_cat(buf, tmp, 128); sm_str_cat(buf, " seconds", 128);
    sm_print(buf, SM_TEXT_FG);

    framebuffer_t *fb = fb_info();
    if (fb && fb_ready()) {
        sm_str_copy(buf, "  Display: ", 128);
        char wbuf[12], hbuf[12];
        sm_uint_to_str(fb->width, wbuf); sm_uint_to_str(fb->height, hbuf);
        sm_str_cat(buf, wbuf, 128); sm_str_cat(buf, "x", 128);
        sm_str_cat(buf, hbuf, 128); sm_str_cat(buf, " 32bpp", 128);
        sm_print(buf, SM_TEXT_FG);
    } else {
        sm_print("  Display: not available", SM_HEADER_FG);
    }

    sm_str_copy(buf, "  Reason : ", 128);
    sm_str_cat(buf, sm_reason, 128);
    sm_print(buf, SM_HEADER_FG);

    vfs_node_t *root = vfs_root();
    if (root) {
        sm_print("", SM_TEXT_FG);
        sm_print("  Files in /:", SM_GREEN_FG);
        vfs_node_t *child = root->children;
        if (!child) {
            sm_print("    (empty)", SM_DIM_FG);
        }
        while (child) {
            char line[SM_LINE_LEN];
            sm_str_copy(line, "    ", SM_LINE_LEN);
            sm_str_cat(line, child->name, SM_LINE_LEN);
            if (child->type == VFS_TYPE_DIR) sm_str_cat(line, "/", SM_LINE_LEN);
            sm_print(line, SM_TEXT_FG);
            child = child->next_sibling;
        }
    }
}

/* Power Actions */
static void sm_action_restart(void) {
    /* Trigger triple fault via invalid IDT, reliable reboot on real x86 */
    __asm__ volatile (
        "cli\n"
        "lidt %0\n"
        "int $0\n"
        :
        : "m"(*(uint64_t *)0)
        :
    );
    for (;;) __asm__ volatile("hlt");
}

static void sm_action_shutdown(void) {
    /* ACPI shutdown shortcut recognised by QEMU (i440fx/piix4 and q35) */
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    /* Older QEMU / Bochs shutdown port, tried as a fallback */
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
    /* If neither worked (e.g. real hardware with no ACPI support here),
     * there's nothing more we can safely do — halt so it's at least
     * safe to cut the power by hand. */
    for (;;) __asm__ volatile("hlt");
}

/* Rendering */
static void sm_layout_buttons(uint32_t sw, uint32_t sh) {
    uint32_t total_w = (uint32_t)SM_BUTTON_COUNT * SM_BTN_W + (SM_BUTTON_COUNT - 1) * SM_BTN_GAP;
    uint32_t start_x = (sw > total_w) ? (sw - total_w) / 2 : 0;
    uint32_t y = sh - SM_STATUS_H - SM_BTN_H - 24;

    for (uint32_t i = 0; i < SM_BUTTON_COUNT; i++) {
        sm_buttons[i].x = start_x + i * (SM_BTN_W + SM_BTN_GAP);
        sm_buttons[i].y = y;
    }
}

static void sm_render(int32_t mouse_x, int32_t mouse_y) {
    if (!fb_ready()) return;

    framebuffer_t *fb = fb_info();
    uint32_t sw = fb->width;
    uint32_t sh = fb->height;

    fb_fill_rect(0, 0, sw, sh, SM_BG);

    /* Header bar */
    fb_fill_rect(0, 0, sw, SM_HEADER_H, SM_HEADER_BG);
    text_draw(16, 8,  "MiraOS  SAFE MODE", SM_HEADER_FG, SM_HEADER_BG);
    text_draw(16, 26, sm_reason,          SM_DIM_FG,    SM_HEADER_BG);

    /* Diagnostics log */
    uint32_t term_top = SM_HEADER_H + 12;
    for (uint32_t i = 0; i < sm_line_count; i++) {
        uint32_t y = term_top + i * SM_FONT_H;
        if (y + SM_FONT_H > sh - SM_STATUS_H - SM_BTN_H - 40)
            break; /* don't draw over the buttons */
        text_draw(8, y, sm_lines[i], sm_line_colors[i], SM_BG);
    }

    /* Buttons */
    sm_layout_buttons(sw, sh);
    for (uint32_t i = 0; i < SM_BUTTON_COUNT; i++) {
        sm_button_t *b = &sm_buttons[i];
        bool hover = sm_point_in_rect(mouse_x, mouse_y, b->x, b->y, b->w, b->h);

        fb_fill_rect(b->x, b->y, b->w, b->h, hover ? SM_BTN_BG_HOVER : SM_BTN_BG);
        gfx_draw_rect_outline(b->x, b->y, b->w, b->h, hover ? SM_BTN_BORDER_HOVER : SM_BTN_BORDER, 2);

        uint32_t label_w = (uint32_t)text_width(b->label) * SM_FONT_W;
        uint32_t tx = b->x + (b->w > label_w ? (b->w - label_w) / 2 : 0);
        uint32_t ty = b->y + (b->h - SM_FONT_H) / 2;
        text_draw(tx, ty, b->label, SM_BTN_TEXT, hover ? SM_BTN_BG_HOVER : SM_BTN_BG);
    }

    /* Mouse cursor */
    fb_fill_rect((uint32_t)mouse_x, (uint32_t)mouse_y, 3, 12, 0xFFFFFF);
    fb_fill_rect((uint32_t)mouse_x, (uint32_t)mouse_y, 12, 3, 0xFFFFFF);

    /* Status bar */
    fb_fill_rect(0, sh - SM_STATUS_H, sw, SM_STATUS_H, 0x110000);
    text_draw(8, sh - SM_STATUS_H + 4,
              "Click a button to continue  |  safe mode  |  no apps loaded",
              SM_DIM_FG, 0x110000);

    fb_swap_buffers();
}

/* Entry Point */
bool safemode_is_active(void) { return sm_active; }

void safemode_enter(const char *reason) {
    sm_active = true;
    if (reason && *reason)
        sm_str_copy(sm_reason, reason, sizeof(sm_reason));
    else
        sm_str_copy(sm_reason, "unknown", sizeof(sm_reason));

    sm_line_count = 0;
    sm_collect_diagnostics();

    bool prev_button_down = false;

    for (;;) {
        mouse_poll();
        int32_t mx = mouse_get_x();
        int32_t my = mouse_get_y();
        bool button_down = mouse_get_button(0);

        /* Fire on the down-edge (the moment the click begins) */
        if (button_down && !prev_button_down) {
            for (uint32_t i = 0; i < SM_BUTTON_COUNT; i++) {
                sm_button_t *b = &sm_buttons[i];
                if (sm_point_in_rect(mx, my, b->x, b->y, b->w, b->h)) {
                    b->action(); /* never returns */
                }
            }
        }
        prev_button_down = button_down;

        sm_render(mx, my);
        __asm__ volatile("hlt");
    }
}