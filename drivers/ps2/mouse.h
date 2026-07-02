#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MOUSE_IRQ 12

void mouse_init(void);
void mouse_poll(void);
int32_t mouse_get_x(void);
int32_t mouse_get_y(void);
bool mouse_get_button(uint8_t button);
bool mouse_has_moved(void);
void mouse_clear_moved(void);