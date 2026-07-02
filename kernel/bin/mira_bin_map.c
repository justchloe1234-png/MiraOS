#include "mira_bin_map.h"

#include "arch/x86_64/paging.h"
#include "kernel/mem.h"
#include "kernel/heap.h"
#include "lib/common/string.h"

static uint64_t mira_flags_to_paging(uint32_t sect_type) {
    uint64_t flags = PAGE_PRESENT;
    /* Bootstrap: keep everything mapped writable for now. */
    if (sect_type != MIRA_SECT_RODATA)
        flags |= PAGE_WRITE;
    return flags;
}

bool mira_map_section(const void *data, size_t size, const mira_sect_desc_t *sd) {
    if (!data || !sd)
        return false;

    if (sd->filesz > sd->memsz)
        return false;
    if (sd->file_off + sd->filesz > size)
        return false;

    uint64_t vaddr = sd->vaddr;
    uint64_t memsz = sd->memsz;
    uint64_t filesz = sd->filesz;

    uint64_t paging_flags = mira_flags_to_paging(sd->sect_type);

    for (uint64_t j = 0; j < memsz; j += 4096) {
        void *page = pmm_alloc_page();
        if (!page)
            return false;

        memset(page, 0, 4096);

        uint64_t src_off = j;
        if (src_off < filesz) {
            uint64_t copy_left = filesz - src_off;
            uint64_t copy = copy_left < 4096 ? copy_left : 4096;
            const uint8_t *src = (const uint8_t *)data + sd->file_off + src_off;
            uint8_t *dst = (uint8_t *)page;
            for (uint64_t k = 0; k < copy; k++)
                dst[k] = src[k];
        }

        /* Temporary: map writable until NX/RO is implemented. */
        paging_map_page(vaddr + j, (uint64_t)page, paging_flags | PAGE_WRITE);
    }

    return true;
}

