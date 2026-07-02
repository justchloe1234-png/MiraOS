#include "mouse.h"
#include "driver.h"
#include "arch/x86_64/cpu.h"
#include <stddef.h>

static int32_t mouse_x = 0;
static int32_t mouse_y = 0;
static uint8_t mouse_buttons = 0;
static bool mouse_moved = false;
static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 1))
                return;
        }
    } else {
        while (timeout--) {
            if (!(inb(0x64) & 2))
                return;
        }
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, data);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

static void mouse_irq_handler(uint8_t irq) {
    (void)irq;
    
    switch (mouse_cycle) {
        case 0:
            mouse_packet[0] = inb(0x60);
            if (mouse_packet[0] & 0x08)
                mouse_cycle++;
            break;
        case 1:
            mouse_packet[1] = inb(0x60);
            mouse_cycle++;
            break;
        case 2:
            mouse_packet[2] = inb(0x60);
            mouse_cycle = 0;
            
            int8_t dx = mouse_packet[1];
            int8_t dy = mouse_packet[2];
            
            if (mouse_packet[0] & 0x10)
                dx |= 0xF0;
            if (mouse_packet[0] & 0x20)
                dy |= 0xF0;
            
            mouse_x += dx;
            mouse_y -= dy;
            
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            
            mouse_buttons = mouse_packet[0] & 0x07;
            mouse_moved = true;
            break;
    }
}

static int mouse_init_driver(void) {
    mouse_wait(1);
    outb(0x64, 0xA8);
    
    mouse_wait(1);
    mouse_write(0xF4);
    mouse_read();
    
    mouse_x = 0;
    mouse_y = 0;
    mouse_buttons = 0;
    mouse_moved = false;
    mouse_cycle = 0;
    
    return 0;
}

static driver_t mouse_driver = {
    .name = "PS/2 Mouse",
    .id = 4,
    .init = mouse_init_driver,
    .irq = mouse_irq_handler,
    .next = 0
};

DRIVER_REGISTER(mouse_driver);

void mouse_init(void) {
    mouse_init_driver();
}

void mouse_poll(void) {
}

int32_t mouse_get_x(void) {
    return mouse_x;
}

int32_t mouse_get_y(void) {
    return mouse_y;
}

bool mouse_get_button(uint8_t button) {
    return (mouse_buttons & (1 << button)) != 0;
}

bool mouse_has_moved(void) {
    return mouse_moved;
}

void mouse_clear_moved(void) {
    mouse_moved = false;
}