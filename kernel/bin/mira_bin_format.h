#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Binary container ABI for MiraOS enterprise runtime.
 * Split from loader implementation to keep modules clean.
 */

#define MIRA_BIN_MAGIC 0x4D495845582ULL /* "MIRAHEX" */
#define MIRA_BIN_ABI_VERSION 1

#define MIRA_BIN_TYPE_MEX 1
#define MIRA_BIN_TYPE_MDL 2

#define MIRA_SECT_TEXT 1
#define MIRA_SECT_RODATA 2
#define MIRA_SECT_DATA 3
#define MIRA_SECT_BSS 4

#define MIRA_BIN_FLAG_X  (1u << 0)
#define MIRA_BIN_FLAG_W  (1u << 1)
#define MIRA_BIN_FLAG_R  (1u << 2)

#pragma pack(push, 1)
typedef struct mira_bin_header {
    uint64_t magic;
    uint16_t abi_version;
    uint16_t type;

    uint32_t header_size;
    uint32_t image_flags;

    uint64_t entry_point;
    uint64_t load_base;

    uint32_t sect_count;

    uint64_t sect_table_off;
    uint64_t name_table_off;

    uint64_t checksum;
} mira_bin_header_t;

typedef struct mira_sect_desc {
    uint32_t sect_type;
    uint32_t sect_flags;

    uint64_t vaddr;
    uint64_t memsz;
    uint64_t filesz;
    uint64_t file_off;
} mira_sect_desc_t;
#pragma pack(pop)

typedef struct mira_loaded_image {
    uint64_t entry_point;
    uint64_t cr3;
    uint64_t user_stack_top;
} mira_loaded_image_t;

bool mira_bin_is_valid(const void *data, size_t size, uint16_t expected_type);

