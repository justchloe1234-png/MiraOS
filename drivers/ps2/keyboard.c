#include "keyboard.h"
#include "driver.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/isr.h"

#define KBD_BUF_SIZE 128

static uint8_t kbd_buffer[KBD_BUF_SIZE];
static int kbd_head;
static int kbd_tail;
static int shift_pressed;
static int caps_lock;

static char scancode_map(uint8_t sc, int shift) {
    static const char normal[] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' '
    };
    static const char shifted[] = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
        '*', 0, ' '
    };
    if (sc >= sizeof(normal))
        return 0;
    char c = shift ? shifted[sc] : normal[sc];
    if (!shift && caps_lock && c >= 'a' && c <= 'z')
        c = c - 'a' + 'A';
    if (shift && caps_lock && c >= 'A' && c <= 'Z')
        c = c - 'A' + 'a';
    return c;
}

static int kbd_push(uint8_t sc) {
    int next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail)
        return -1;
    kbd_buffer[kbd_head] = sc;
    kbd_head = next;
    return 0;
}


static int keyboard_driver_init(void) {
    keyboard_init();
    return 0;
}

static void keyboard_driver_irq(uint8_t irq) {
    (void)irq;
    keyboard_on_irq();
}

static driver_t keyboard_driver = {
    .name = "ps2kbd",
    .id = DRIVER_ID_KEYBOARD,
    .init = keyboard_driver_init,
    .irq = keyboard_driver_irq,
    .next = 0
};

DRIVER_REGISTER(keyboard_driver);

void keyboard_init(void) {
    kbd_head = 0;
    kbd_tail = 0;
    shift_pressed = 0;
    caps_lock = 0;
    while (inb(0x64) & 1)
        inb(0x60);
}

void keyboard_on_irq(void) {
    uint8_t sc = inb(0x60);
    if (sc == 0x2A || sc == 0x36)
        shift_pressed = 1;
    else if (sc == 0xAA || sc == 0xB6)
        shift_pressed = 0;
    else if (sc == 0x3A)
        caps_lock = !caps_lock;
    else if (sc < 0x80)
        kbd_push(sc);
}

bool keyboard_poll(uint8_t *scancode) {
    if (kbd_head == kbd_tail)
        return false;
    *scancode = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return true;
}

char keyboard_scancode_to_ascii(uint8_t scancode) {
    if (scancode >= 0x80)
        return 0;
    return scancode_map(scancode, shift_pressed ^ caps_lock);
}
