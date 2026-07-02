#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "mira_bin_format.h"

/* Loader API (exec/module). Implementation split from format.
 *
 * NOTE: `mira_bin_format.h` defines the stable on-disk structs.
 */

bool mira_load_mex(const void *data, size_t size, mira_loaded_image_t *out);
bool mira_load_mdl(const void *data, size_t size, mira_loaded_image_t *out);


