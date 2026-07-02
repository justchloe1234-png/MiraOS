#include "mira_bin_validate.h"

#include "mira_bin_checksum.h"
#include "lib/common/string.h"

bool mira_validate_header(const void *data, size_t size, uint16_t expected_type) {
    if (!data)
        return false;
    if (size < sizeof(mira_bin_header_t))
        return false;

    const mira_bin_header_t *hdr = (const mira_bin_header_t *)data;

    if (hdr->magic != MIRA_BIN_MAGIC)
        return false;
    if (hdr->abi_version != MIRA_BIN_ABI_VERSION)
        return false;
    if (hdr->type != expected_type)
        return false;
    if (hdr->header_size < sizeof(mira_bin_header_t))
        return false;
    if (hdr->sect_count > 65535)
        return false;
    if (hdr->sect_table_off < sizeof(mira_bin_header_t))
        return false;
    if (hdr->sect_table_off >= size)
        return false;

    size_t sect_desc_bytes = (size_t)hdr->sect_count * sizeof(mira_sect_desc_t);
    if (hdr->sect_table_off + sect_desc_bytes > size)
        return false;

    /* Verify checksum (bootstrap): header with checksum field zeroed + remainder as-is. */
    uint64_t expected = hdr->checksum;

    uint64_t h = 1469598103934665603ULL;

    /* hash header with checksum zeroed */
    mira_bin_header_t tmp = *hdr;
    tmp.checksum = 0;
    const uint8_t *hdr_zero = (const uint8_t *)&tmp;
    for (size_t i = 0; i < sizeof(mira_bin_header_t); i++) {
        h ^= (uint64_t)hdr_zero[i];
        h *= 1099511628211ULL;
    }

    /* hash remainder as-is */
    const uint8_t *bytes = (const uint8_t *)data;
    size_t hdr_bytes = sizeof(mira_bin_header_t);
    if (size < hdr_bytes)
        return false;

    for (size_t i = hdr_bytes; i < size; i++) {
        h ^= (uint64_t)bytes[i];
        h *= 1099511628211ULL;
    }

    return h == expected;
}

