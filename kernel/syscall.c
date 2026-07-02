#include "syscall.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/gdt.h"
#include "fs/vfs.h"
#include "drivers/timer.h"
#include "ui/layout/gfx.h"
#include "ui/layout/text.h"
#include "kernel/process.h"

extern void syscall_entry(void);

struct syscall_cpu {
    uint64_t user_rsp;
    uint64_t kernel_rsp;
} __attribute__((aligned(16)));

static struct syscall_cpu syscall_cpu_data;
static uint8_t syscall_stack[16384] __attribute__((aligned(16)));

void syscall_init(void) {
    syscall_cpu_data.user_rsp = 0;
    syscall_cpu_data.kernel_rsp = (uint64_t)&syscall_stack[sizeof(syscall_stack)];

    uint64_t efer = rdmsr(0xC0000080);
    wrmsr(0xC0000080, efer | (1 << 0));

    uint64_t star = ((uint64_t)GDT_KERNEL_CODE << 32) | ((uint64_t)(GDT_USER_CODE | 3) << 48);
    wrmsr(0xC0000081, star);
    wrmsr(0xC0000082, (uint64_t)syscall_entry);
    wrmsr(0xC0000084, 1 << 9);

    wrmsr(0xC0000101, (uint64_t)&syscall_cpu_data);
    wrmsr(0xC0000102, (uint64_t)&syscall_cpu_data);
}

int64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4;
    (void)a5;

    switch (num) {
    case SYSCALL_EXIT: {
        process_t *cur = process_get_current();
        if (cur) {
            cur->exit_status = (int)a1;
            cur->state = PROCESS_STATE_ZOMBIE;
            scheduler_remove(cur);
            process_destroy(cur);
        }
        scheduler_tick();
        return 0;
    }
    case SYSCALL_READ:
        return vfs_read((int)a1, (void *)a2, (size_t)a3);
    case SYSCALL_WRITE:
        return vfs_write((int)a1, (const void *)a2, (size_t)a3);
    case SYSCALL_OPEN:
        return vfs_open((const char *)a1, (int)a2);
    case SYSCALL_CLOSE:
        return vfs_close((int)a1);
    case SYSCALL_DRAW_RECT:
        gfx_draw_rect((uint32_t)a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)a4, (uint32_t)a5);
        return 0;
    case SYSCALL_DRAW_TEXT:
        text_draw((uint32_t)a1, (uint32_t)a2, (const char *)a3, (uint32_t)a4, (uint32_t)a5);
        return 0;
    case SYSCALL_GET_TICKS:
        return (int64_t)timer_ticks();
    default:
        return -1;
    }
}
