#include "paging.h"
#include "cpu.h"
#include "kernel/mem.h"
#include "kernel/panic.h"

#define PML4_INDEX(v) (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v) (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)   (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)   (((v) >> 12) & 0x1FF)

#define HEAP_VIRT_BASE 0x40000000ULL

static uint64_t boot_pml4[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t boot_pt[512]   __attribute__((aligned(PAGE_SIZE)));
static uint64_t boot_pd[512]   __attribute__((aligned(PAGE_SIZE)));
static uint64_t boot_pdpt[512] __attribute__((aligned(PAGE_SIZE)));
static uint64_t *pml4_root;

static uint64_t *table_alloc(void) {
    void *phys = pmm_alloc_page();
    if (!phys)
        panic("paging oom");
    /* Identity-map the new page so we can zero it safely. */
    paging_map_page((uint64_t)phys, (uint64_t)phys, 0);
    uint64_t *t = (uint64_t *)phys;
    for (int i = 0; i < 512; i++)
        t[i] = 0;
    return t;
}

static uint64_t *walk(uint64_t virt, int create) {
    uint64_t *pml4 = pml4_root;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;

    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) {
        if (!create) return 0;
        pdpt = table_alloc();
        pml4[PML4_INDEX(virt)] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_WRITE;
    } else {
        pdpt = (uint64_t *)(pml4[PML4_INDEX(virt)] & ~0xFFFULL);
    }

    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) {
        if (!create) return 0;
        pd = table_alloc();
        pdpt[PDPT_INDEX(virt)] = (uint64_t)pd | PAGE_PRESENT | PAGE_WRITE;
    } else {
        pd = (uint64_t *)(pdpt[PDPT_INDEX(virt)] & ~0xFFFULL);
    }

    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) {
        if (!create) return 0;
        pt = table_alloc();
        pd[PD_INDEX(virt)] = (uint64_t)pt | PAGE_PRESENT | PAGE_WRITE;
    } else {
        if (pd[PD_INDEX(virt)] & PAGE_SIZE_FLAG)
            return 0;
        pt = (uint64_t *)(pd[PD_INDEX(virt)] & ~0xFFFULL);
    }

    return pt;
}

void paging_init(void) {
    /* Use static boot_pml4 — no PMM allocation needed before the map is live. */
    for (int i = 0; i < 512; i++)
        boot_pml4[i] = 0;
    pml4_root = boot_pml4;

    /* Identity-map first 2 MB via static tables. */
    for (int i = 0; i < 512; i++)
        boot_pt[i] = (uint64_t)(i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;

    boot_pd[0]   = (uint64_t)&boot_pt   | PAGE_PRESENT | PAGE_WRITE;
    boot_pdpt[0] = (uint64_t)&boot_pd   | PAGE_PRESENT | PAGE_WRITE;
    pml4_root[0] = (uint64_t)&boot_pdpt | PAGE_PRESENT | PAGE_WRITE;

    write_cr3((uint64_t)pml4_root);
    uint64_t cr4 = read_cr4();
    write_cr4(cr4 | (1 << 5));
}

void *paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t *pt = walk(virt, 1);
    if (!pt)
        panic("paging walk fail");
    pt[PT_INDEX(virt)] = (phys & ~0xFFFULL) | flags | PAGE_PRESENT | PAGE_WRITE;
    invlpg((void *)virt);
    return (void *)virt;
}

void paging_unmap_page(uint64_t virt) {
    uint64_t *pt = walk(virt, 0);
    if (!pt)
        return;
    pt[PT_INDEX(virt)] = 0;
    invlpg((void *)virt);
}

uint64_t paging_virt_to_phys(uint64_t virt) {
    uint64_t *pt = walk(virt, 0);
    if (!pt)
        return 0;
    uint64_t entry = pt[PT_INDEX(virt)];
    if (!(entry & PAGE_PRESENT))
        return 0;
    return (entry & ~0xFFFULL) + (virt & 0xFFF);
}

uint64_t paging_heap_base(void) {
    return HEAP_VIRT_BASE;
}
