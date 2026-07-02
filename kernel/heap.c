#include "heap.h"
#include "arch/x86_64/paging.h"
#include "kernel/mem.h"
#include "kernel/panic.h"
#include "lib/mem.h"

#define HEAP_SIZE (16 * 1024 * 1024)
#define MIN_BLOCK 32
#define HEADER_SIZE sizeof(struct block_header)
#define SLAB_MAX_ORDER 5
#define SLAB_MIN_SIZE 32
#define SLAB_MAX_SIZE (SLAB_MIN_SIZE << SLAB_MAX_ORDER)

struct block_header {
    size_t size;
    int free;
    struct block_header *next;
    struct block_header *prev;
};

static struct block_header *heap_head;
static uint64_t heap_mapped;
static uint64_t heap_virt_base;

/* Slab allocator for small objects */
typedef struct slab_cache {
    size_t object_size;
    size_t slab_size;
    struct slab_cache *next;
    void *free_list;
    uint32_t objects_per_slab;
    uint32_t allocated;
} slab_cache_t;

static slab_cache_t slab_caches[SLAB_MAX_ORDER];
static slab_cache_t *slab_list = 0;

static void slab_init(void) {
    for (int i = 0; i < SLAB_MAX_ORDER; i++) {
        slab_caches[i].object_size = SLAB_MIN_SIZE << i;
        slab_caches[i].slab_size = 4096;
        slab_caches[i].next = (i < SLAB_MAX_ORDER - 1) ? &slab_caches[i + 1] : 0;
        slab_caches[i].free_list = 0;
        slab_caches[i].objects_per_slab = 4096 / slab_caches[i].object_size;
        slab_caches[i].allocated = 0;
    }
    slab_list = &slab_caches[0];
}

static void *slab_alloc(slab_cache_t *cache) {
    if (!cache->free_list) {
        /* Allocate new slab */
        void *slab = pmm_alloc_page();
        if (!slab)
            return 0;
        
        paging_map_page(heap_virt_base + heap_mapped, (uint64_t)slab, 0);
        heap_mapped += 4096;
        
        /* Initialize free list */
        uint8_t *ptr = (uint8_t *)(heap_virt_base + heap_mapped - 4096);
        cache->free_list = ptr;
        
        for (uint32_t i = 0; i < cache->objects_per_slab - 1; i++) {
            void **next = (void **)(ptr + i * cache->object_size);
            *next = ptr + (i + 1) * cache->object_size;
        }
        void **last = (void **)(ptr + (cache->objects_per_slab - 1) * cache->object_size);
        *last = 0;
    }
    
    void *obj = cache->free_list;
    cache->free_list = *(void **)cache->free_list;
    cache->allocated++;
    return obj;
}

static slab_cache_t *slab_find_cache(size_t size) {
    slab_cache_t *cache = slab_list;
    while (cache) {
        if (cache->object_size >= size)
            return cache;
        cache = cache->next;
    }
    return 0;
}

static void heap_map_more(uint64_t needed) {
    while (heap_mapped < needed) {
        void *phys = pmm_alloc_page();
        if (!phys)
            panic("heap oom");
        paging_map_page(heap_virt_base + heap_mapped, (uint64_t)phys, 0);
        heap_mapped += 4096;
    }
}

void heap_init(void) {
    heap_virt_base = paging_heap_base();
    heap_mapped = 0;
    heap_map_more(4096);

    heap_head = (struct block_header *)heap_virt_base;
    heap_head->size = 4096 - HEADER_SIZE;
    heap_head->free = 1;
    heap_head->next = 0;
    heap_head->prev = 0;
    
    slab_init();
}

static struct block_header *split_block(struct block_header *block, size_t size) {
    if (block->size < size + HEADER_SIZE + MIN_BLOCK)
        return block;

    struct block_header *new_block = (struct block_header *)((uint8_t *)block + HEADER_SIZE + size);
    new_block->size = block->size - size - HEADER_SIZE;
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;
    if (block->next)
        block->next->prev = new_block;
    block->next = new_block;
    block->size = size;
    return block;
}

static void merge_adjacent(struct block_header *block) {
    if (block->next && block->next->free) {
        block->size += HEADER_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next)
            block->next->prev = block;
    }
    if (block->prev && block->prev->free) {
        block->prev->size += HEADER_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next)
            block->next->prev = block->prev;
    }
}

void *kmalloc(size_t size) {
    if (size == 0)
        return 0;

    /* Use slab allocator for small objects */
    if (size <= SLAB_MAX_SIZE) {
        slab_cache_t *cache = slab_find_cache(size);
        if (cache)
            return slab_alloc(cache);
    }

    size = (size + 15) & ~15;
    struct block_header *current = heap_head;

    while (current) {
        if (current->free && current->size >= size) {
            current = split_block(current, size);
            current->free = 0;
            return (uint8_t *)current + HEADER_SIZE;
        }
        current = current->next;
    }

    uint64_t needed = heap_mapped + 4096;
    heap_map_more(needed);

    struct block_header *new_area = (struct block_header *)(heap_virt_base + heap_mapped - 4096);
    if (heap_head) {
        struct block_header *tail = heap_head;
        while (tail->next)
            tail = tail->next;
        tail->next = new_area;
        new_area->prev = tail;
    } else {
        heap_head = new_area;
    }
    new_area->size = 4096 - HEADER_SIZE;
    new_area->free = 1;
    new_area->next = 0;

    return kmalloc(size);
}

void kfree(void *ptr) {
    if (!ptr)
        return;
    struct block_header *block = (struct block_header *)((uint8_t *)ptr - HEADER_SIZE);
    block->free = 1;
    merge_adjacent(block);
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return 0;
    }
    struct block_header *block = (struct block_header *)((uint8_t *)ptr - HEADER_SIZE);
    if (block->size >= size)
        return ptr;
    void *new_ptr = kmalloc(size);
    if (!new_ptr)
        return 0;
    for (size_t i = 0; i < block->size; i++)
        ((uint8_t *)new_ptr)[i] = ((uint8_t *)ptr)[i];
    kfree(ptr);
    return new_ptr;
}

void *kcalloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = kmalloc(total);
    if (ptr) {
        for (size_t i = 0; i < total; i++)
            ((uint8_t *)ptr)[i] = 0;
    }
    return ptr;
}

size_t heap_used(void) {
    size_t used = 0;
    struct block_header *current = heap_head;
    while (current) {
        if (!current->free)
            used += current->size;
        current = current->next;
    }
    return used;
}

size_t heap_total(void) {
    return heap_mapped;
}