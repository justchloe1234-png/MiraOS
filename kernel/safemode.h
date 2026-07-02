#pragma once

#include <stdint.h>
#include <stdbool.h>

void safemode_enter(const char *reason);
bool safemode_is_active(void);