#include "mira_exec.h"

#include "kernel.h"
#include "kernel/panic.h"
#include "kernel/mem.h"
#include "kernel/heap.h"
#include "arch/x86_64/paging.h"
#include "lib/common/string.h"
#include "lib/common/mem.h"

#include "mira_bin_format.h"


/* First milestone loader:
 * - Validate container header.
 * - Map each section as pages into the CURRENT paging context using paging_map_page.
 *
 * Note: this does NOT yet create per-process page tables.
 * It only bootstraps the on-disk format and the code that parses it.
 */







static bool mira_validate_header(const mira_bin_header_t *hdr, size_t size, uint16_t expected_type) {
    if (!hdr)
        return false;
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

    /* Bounds check for section descriptor table */
    size_t sect_desc_bytes = (size_t)hdr->sect_count * sizeof(mira_sect_desc_t);
    if (hdr->sect_table_off + sect_desc_bytes > size)
        return false;

    /* Validate checksum */
    /* checksum field is part of header; to validate it we zero it temporarily */
    mira_bin_header_t tmp;
    /* Safe because header is fixed-size in this milestone */
    if (hdr->header_size > sizeof(mira_bin_header_t)) {
        /* We don't support extra fixed extras yet */
        return false;
    }
    tmp = *hdr;
    uint64_t expected = tmp.checksum;
    tmp.checksum = 0;

    /* Compute checksum over the whole file, with checksum field zeroed.
       Since we can’t modify the caller buffer, copy just the header, checksum field is zeroed.
       Then checksum(header+sections) by re-hashing in two parts. */
    const uint8_t *bytes = (const uint8_t *)hdr;
    uint64_t h = 1469598103934665603ULL;

    /* hash header with checksum zeroed */
    const uint8_t *hdr_zero = (const uint8_t *)&tmp;
    for (size_t i = 0; i < sizeof(mira_bin_header_t); i++) {
        h ^= (uint64_t)hdr_zero[i];
        h *= 1099511628211ULL;
    }

    /* hash remainder of file as-is */
    size_t hdr_bytes = sizeof(mira_bin_header_t);
    if (size < hdr_bytes)
        return false;
    for (size_t i = hdr_bytes; i < size; i++) {
        h ^= (uint64_t)bytes[i];
        h *= 1099511628211ULL;
    }

    return h == expected;
}

static uint64_t mira_flags_to_paging(uint32_t sect_type, uint32_t sect_flags) {
    (void)sect_flags;
    /* Paging flags mapping: R/W and executable.
       Since paging_map_page only exposes PRESENT|WRITE right now,
       we use write for data, and omit write for text/rodata in first milestone. */
    uint64_t flags = PAGE_PRESENT;
    if (sect_type == MIRA_SECT_DATA || sect_type == MIRA_SECT_BSS || sect_type == MIRA_SECT_TEXT) {
        /* .text is readable+exec; WRITE may be allowed by this milestone.
           Keep it write-off for better isolation later. */
        if (sect_type != MIRA_SECT_TEXT)
            flags |= PAGE_WRITE;
    } else if (sect_type == MIRA_SECT_RODATA) {
        /* read-only => no PAGE_WRITE */
    }
    return flags;
}

static bool mira_load(const void *data, size_t size, uint16_t type, mira_loaded_image_t *out) {
    if (!data || size < sizeof(mira_bin_header_t) || !out)
        return false;

    const mira_bin_header_t *hdr = (const mira_bin_header_t *)data;
    if (!mira_validate_header(hdr, size, type))
        return false;

    const mira_sect_desc_t *sectors = (const mira_sect_desc_t *)((const uint8_t *)data + hdr->sect_table_off);

    /* Load sections by mapping pages at vaddr.
       This milestone assumes loader runs in a single address space. */
    for (uint32_t i = 0; i < hdr->sect_count; i++) {
        const mira_sect_desc_t *sd = &sectors[i];

        if (sd->filesz > sd->memsz)
            return false;
        if (sd->file_off + sd->filesz > size)
            return false;

        uint64_t vaddr = sd->vaddr;
        uint64_t memsz = sd->memsz;
        uint64_t filesz = sd->filesz;

        uint64_t paging_flags = mira_flags_to_paging(sd->sect_type, sd->sect_flags);

        for (uint64_t j = 0; j < memsz; j += 4096) {
            void *page = pmm_alloc_page();
            if (!page)
                return false;

            /* Zero page for BSS and partial last pages */
            memset(page, 0, 4096);

            uint64_t dst_off = j;
            uint64_t src_off = dst_off;

            /* Copy min(filesz, memsz) into page */
            if (src_off < filesz) {
                uint64_t copy_left = filesz - src_off;
                uint64_t copy = copy_left < 4096 ? copy_left : 4096;

                const uint8_t *src = (const uint8_t *)data + sd->file_off + src_off;
                uint8_t *dst = (uint8_t *)page;
                for (uint64_t k = 0; k < copy; k++)
                    dst[k] = src[k];
            }

            paging_map_page(vaddr + j, (uint64_t)page, paging_flags | PAGE_WRITE);
            /* Note: temporarily we always add PAGE_WRITE because paging_map_page ignores W.
               We will revisit this once NX/RO is implemented. */
        }
    }

    out->entry_point = hdr->entry_point;
    out->cr3 = 0;
    out->user_stack_top = 0;
    return true;
}

bool mira_bin_is_valid(const void *data, size_t size, uint16_t expected_type) {
    if (!data)
        return false;
    if (size < sizeof(mira_bin_header_t))
        return false;
    const mira_bin_header_t *hdr = (const mira_bin_header_t *)data;
    return mira_validate_header(hdr, size, expected_type);
}

bool mira_load_mex(const void *data, size_t size, mira_loaded_image_t *out) {
    return mira_load(data, size, MIRA_BIN_TYPE_MEX, out);
}

bool mira_load_mdl(const void *data, size_t size, mira_loaded_image_t *out) {
    return mira_load(data, size, MIRA_BIN_TYPE_MDL, out);
}

