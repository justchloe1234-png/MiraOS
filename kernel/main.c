#include "kernel.h"
#include "kernel/panic.h"
#include "kernel/mem.h"
#include "kernel/heap.h"
#include "kernel/syscall.h"
#include "kernel/process.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/isr.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/cpu.h"
#include "drivers/driver.h"
#include "drivers/framebuffer.h"
#include "drivers/timer.h"
#include "drivers/storage/ata.h"
#include "fs/backend/ramfs.h"
#include "net/net.h"
#include "ui/ui.h"

static void early_vga_write(const char *s) {
    static uint16_t *video = (uint16_t *)0xB8000;
    static size_t cursor = 0;
    for (const char *p = s; *p; ++p) {
        if (*p == '\n') {
            cursor = (cursor / 80 + 1) * 80;
            continue;
        }
        if (cursor >= 80 * 25) {
            cursor = 0;
        }
        video[cursor++] = (uint16_t)(0x0F00 | (uint8_t)*p);
    }
}

static inline void serial_init(void) {
    outb(0x3F8 + 1, 0x00);    // disable all interrupts
    outb(0x3F8 + 3, 0x80);    // enable DLAB (set baud rate divisor)
    outb(0x3F8 + 0, 0x03);    // set divisor to 3 (lo byte) 38400 baud
    outb(0x3F8 + 1, 0x00);    //                  (hi byte)
    outb(0x3F8 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(0x3F8 + 2, 0xC7);    // enable FIFO, clear them, with 14-byte threshold
    outb(0x3F8 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static inline void serial_putc(char c) {
    while (!(inb(0x3F8 + 5) & 0x20))
        ;
    outb(0x3F8, (uint8_t)c);
}

static void serial_write(const char *s) {
    for (; *s; ++s) {
        if (*s == '\n')
            serial_putc('\r');
        serial_putc(*s);
    }
}

void kernel_idle(void) {
    uint64_t last_tick = 0;
    for (;;) {
        ui_tick();
        uint64_t t = timer_ticks();
        if (ui_dirty() || t != last_tick) {
            last_tick = t;
            ui_render();
        }
        hlt();
    }
}

void kernel_main(uint64_t magic, uint64_t mbi) {
    serial_init();
    serial_write("MiraOS is booting...\n");

    if (magic != MULTIBOOT2_MAGIC || mbi == 0) {
        early_vga_write("MiraOS booted in a minimal fallback mode.\n");
        serial_write("No multiboot metadata was provided; running the self-contained fallback path.\n");
        for (;;) {
            hlt();
        }
    }

    serial_write("kernel_main entered\n");
    gdt_init();
    pmm_init(mbi);
    paging_init();
    heap_init();
    idt_init();
    isr_init();
    syscall_init();
    process_init();
    scheduler_init();
    serial_write("before fb_init\n");
    fb_init(mbi);
    serial_write("after fb_init\n");
    driver_subsystem_init();
    ata_init();
    ramfs_init();
    net_init();
    ui_init();
    ui_render();
    sti();
    kernel_idle();
}
