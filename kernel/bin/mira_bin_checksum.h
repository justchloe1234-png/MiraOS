#pragma once

#include <stdint.h>
#include <stddef.h>

/* Non-cryptographic checksum for bootstrap integrity.
 * Replace with CRC64/SHA later.
 */

uint64_t mira_checksum64(const void *data, size_t size);

