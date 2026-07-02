#include "input.h"
#include "drivers/ps2/keyboard.h"

static char input_buf[INPUT_MAX];
static size_t input_len;
static bool input_changed;

void input_init(void) {
    input_len = 0;
    input_buf[0] = 0;
    input_changed = false;
}

void input_poll(void) {
    uint8_t sc;
    while (keyboard_poll(&sc)) {
        char c = keyboard_scancode_to_ascii(sc);
        if (c == '\b') {
            if (input_len > 0) {
                input_len--;
                input_buf[input_len] = 0;
                input_changed = true;
            }
        } else if (c == '\n') {
            input_buf[input_len] = 0;
            input_len = 0;
            input_buf[0] = 0;
            input_changed = true;
        } else if (c >= 32 && input_len < INPUT_MAX - 1) {
            input_buf[input_len++] = c;
            input_buf[input_len] = 0;
            input_changed = true;
        }
    }
}

const char *input_buffer(void) {
    return input_buf;
}

size_t input_length(void) {
    return input_len;
}

void input_clear(void) {
    input_len = 0;
    input_buf[0] = 0;
    input_changed = true;
}

bool input_dirty(void) {
    return input_changed;
}

void input_clear_dirty(void) {
    input_changed = false;
}
