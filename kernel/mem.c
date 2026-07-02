#include "mem.h"
#include "kernel.h"
#include "panic.h"

#define MAX_PHYS_PAGES 131072
#define BITMAP_SIZE ((MAX_PHYS_PAGES + 7) / 8)

static uint8_t page_bitmap[BITMAP_SIZE];
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t mem_start;
static uint64_t mem_end;

static struct multiboot_tag *mbi_next(struct multiboot_tag *tag) {
    return (struct multiboot_tag *)((uint8_t *)tag + ((tag->size + 7) & ~7));
}

static void bitmap_set(uint64_t page) {
    page_bitmap[page / 8] |= (1 << (page % 8));
}

static void bitmap_clear(uint64_t page) {
    page_bitmap[page / 8] &= ~(1 << (page % 8));
}

static int bitmap_test(uint64_t page) {
    return page_bitmap[page / 8] & (1 << (page % 8));
}

static void reserve_region(uint64_t base, uint64_t length) {
    uint64_t start_page = base / 4096;
    uint64_t end_page = (base + length + 4095) / 4096;
    for (uint64_t p = start_page; p < end_page; p++)
        bitmap_set(p);
}

void pmm_init(uint64_t mbi) {
    for (size_t i = 0; i < BITMAP_SIZE; i++)
        page_bitmap[i] = 0xFF;

    total_pages = 0;
    free_pages = 0;
    mem_start = 0x100000;
    mem_end = 0;

    struct multiboot_tag *tag = (struct multiboot_tag *)(mbi + 8);
    while (tag->type != 0) {
        if (tag->type == 6) {
            struct multiboot_tag_mmap {
                uint32_t type;
                uint32_t size;
                uint32_t entry_size;
                uint32_t entry_version;
            } *mmap = (void *)tag;

            for (uint8_t *entry = (uint8_t *)tag + sizeof(*mmap);
                 entry < (uint8_t *)tag + mmap->size;
                 entry += mmap->entry_size) {
                struct multiboot_mmap_entry *e = (void *)entry;
                if (e->type != 1)
                    continue;
                uint64_t end = e->base + e->length;
                if (end > mem_end)
                    mem_end = end;
                uint64_t start_page = e->base / 4096;
                uint64_t end_page = end / 4096;
                for (uint64_t p = start_page; p < end_page; p++) {
                    bitmap_clear(p);
                    total_pages++;
                    free_pages++;
                }
            }
        }
        tag = mbi_next(tag);
    }

    reserve_region(0, 0x100000);
    reserve_region((uint64_t)&page_bitmap, sizeof(page_bitmap));
}

void *pmm_alloc_page(void) {
    for (uint64_t p = mem_start / 4096; p < mem_end / 4096; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            free_pages--;
            return (void *)(p * 4096);
        }
    }
    return 0;
}

void pmm_free_page(void *page) {
    uint64_t p = (uint64_t)page / 4096;
    if (!bitmap_test(p))
        return;
    bitmap_clear(p);
    free_pages++;
}

uint64_t pmm_total_pages(void) { return total_pages; }
uint64_t pmm_free_count(void) { return free_pages; }
