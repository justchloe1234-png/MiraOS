#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "mira_bin_format.h"

bool mira_map_section(const void *data, size_t size, const mira_sect_desc_t *sd);

