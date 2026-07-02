#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "mira_bin_format.h"

bool mira_validate_header(const void *data, size_t size, uint16_t expected_type);

