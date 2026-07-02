#pragma once

#include <stdint.h>
#include <stdbool.h>

void keyboard_init(void);
void keyboard_on_irq(void);
bool keyboard_poll(uint8_t *scancode);
char keyboard_scancode_to_ascii(uint8_t scancode);
