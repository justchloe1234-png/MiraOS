#include "process.h"
#include "kernel.h"
#include "kernel/mem.h"
#include "kernel/heap.h"
#include "kernel/panic.h"
#include "lib/common/mem.h"
#include "lib/common/ds.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/paging.h"

static process_t *current_process = 0;
static process_t *process_list = 0;
static uint32_t next_pid = 1;

process_t *process_create(const char *name, uint64_t entry_point) {
    process_t *proc = (process_t *)kmalloc(sizeof(process_t));
    if (!proc)
        return 0;

    memset(proc, 0, sizeof(process_t));

    proc->pid = process_alloc_pid();
    ds_strcpy(proc->name, name);
    proc->state = PROCESS_STATE_READY;
    proc->entry_point = entry_point;

    /* Allocate and map stack pages */
    uint64_t stack_pages = PROCESS_STACK_SIZE / 4096;
    void *stack_base = pmm_alloc_page();
    if (!stack_base) {
        kfree(proc);
        return 0;
    }
    proc->stack_bottom = (uint64_t)stack_base;
    for (uint64_t i = 1; i < stack_pages; i++) {
        void *pg = pmm_alloc_page();
        if (!pg) {
            /* free already allocated pages */
            for (uint64_t j = 0; j < i; j++)
                pmm_free_page((void *)(proc->stack_bottom + j * 4096));
            kfree(proc);
            return 0;
        }
    }
    proc->stack_top = proc->stack_bottom + PROCESS_STACK_SIZE;

    /* Initialize context */
    memset(&proc->context, 0, sizeof(proc->context));
    proc->context.rsp = proc->stack_top;
    proc->context.rip = entry_point;
    proc->context.rflags = 0x202; /* Enable interrupts */

    /* Add to process list */
    proc->next = process_list;
    if (process_list)
        process_list->prev = proc;
    process_list = proc;

    return proc;
}

void process_destroy(process_t *proc) {
    if (!proc)
        return;

    if (proc->stack_bottom) {
        uint64_t stack_pages = PROCESS_STACK_SIZE / 4096;
        for (uint64_t i = 0; i < stack_pages; i++)
            pmm_free_page((void *)(proc->stack_bottom + i * 4096));
    }

    /* Remove from process list */
    if (proc->prev)
        proc->prev->next = proc->next;
    if (proc->next)
        proc->next->prev = proc->prev;
    if (process_list == proc)
        process_list = proc->next;

    kfree(proc);
}

process_t *process_get_current(void) {
    return current_process;
}

void process_set_current(process_t *proc) {
    current_process = proc;
}

static inline void save_minimal(process_t *proc) {
    if (!proc)
        return;

    __asm__ volatile (
        "pushfq\n"
        "popq %0\n"
        : "=m"(proc->context.rflags)
    );

    __asm__ volatile (
        "movq %%rsp, %0\n"
        "movq %%rbp, %1\n"
        : "=m"(proc->context.rsp), "=m"(proc->context.rbp)
    );

    __asm__ volatile (
        "movq %%rax, %0\n"
        "movq %%rbx, %1\n"
        "movq %%rcx, %2\n"
        "movq %%rdx, %3\n"
        "movq %%rdi, %4\n"
        "movq %%rsi, %5\n"
        "movq %%r8,  %6\n"
        "movq %%r9,  %7\n"
        "movq %%r10, %8\n"
        "movq %%r11, %9\n"
        "movq %%r12, %10\n"
        "movq %%r13, %11\n"
        "movq %%r14, %12\n"
        "movq %%r15, %13\n"
        : "=m"(proc->context.rax),
          "=m"(proc->context.rbx),
          "=m"(proc->context.rcx),
          "=m"(proc->context.rdx),
          "=m"(proc->context.rdi),
          "=m"(proc->context.rsi),
          "=m"(proc->context.r8),
          "=m"(proc->context.r9),
          "=m"(proc->context.r10),
          "=m"(proc->context.r11),
          "=m"(proc->context.r12),
          "=m"(proc->context.r13),
          "=m"(proc->context.r14),
          "=m"(proc->context.r15)
    );
}

void process_switch(process_t *next) {
    if (!next)
        return;
    if (current_process == next)
        return;

    if (current_process) {
        /* Save return RIP as a label address */
        __asm__ volatile (
            "leaq 1f(%%rip), %%rax\n"
            "movq %%rax, %0\n"
            : "=m"(current_process->context.rip)
        );
        save_minimal(current_process);
    }

    current_process = next;

    /* Switch address space (per-process CR3) */
    if (next->cr3) {
        write_cr3(next->cr3);
    }

    __asm__ volatile (

        "movq %0, %%rsp\n"
        "movq %1, %%rbp\n"

        "movq %2, %%rax\n"
        "movq %3, %%rbx\n"
        "movq %4, %%rcx\n"
        "movq %5, %%rdx\n"
        "movq %6, %%rdi\n"
        "movq %7, %%rsi\n"
        "movq %8, %%r8\n"
        "movq %9, %%r9\n"
        "movq %10, %%r10\n"
        "movq %11, %%r11\n"
        "movq %12, %%r12\n"
        "movq %13, %%r13\n"
        "movq %14, %%r14\n"
        "movq %15, %%r15\n"

        "pushq %16\n"
        "popfq\n"

        "jmp *%17\n"
        "1:\n"
        :
        : "m"(next->context.rsp),
          "m"(next->context.rbp),
          "m"(next->context.rax),
          "m"(next->context.rbx),
          "m"(next->context.rcx),
          "m"(next->context.rdx),
          "m"(next->context.rdi),
          "m"(next->context.rsi),
          "m"(next->context.r8),
          "m"(next->context.r9),
          "m"(next->context.r10),
          "m"(next->context.r11),
          "m"(next->context.r12),
          "m"(next->context.r13),
          "m"(next->context.r14),
          "m"(next->context.r15),
          "m"(next->context.rflags),
          "m"(next->context.rip)
        : "memory", "cc"
    );
}

void process_init(void) {
    current_process = 0;
    process_list = 0;
    next_pid = 1;
}

uint32_t process_alloc_pid(void) {
    return next_pid++;
}

process_t *process_find_by_pid(uint32_t pid) {
    process_t *proc = process_list;
    while (proc) {
        if (proc->pid == pid)
            return proc;
        proc = proc->next;
    }
    return 0;
}

/* Simple round-robin scheduler */
static process_t *ready_queue = 0;
static process_t *ready_queue_tail = 0;

void scheduler_init(void) {
    ready_queue = 0;
    ready_queue_tail = 0;
}

process_t *scheduler_next(void) {
    return ready_queue;
}

void scheduler_tick(void) {
    if (!ready_queue)
        return;

    /* Rotate: move head to tail */
    if (ready_queue->next) {
        process_t *head = ready_queue;
        ready_queue = head->next;
        ready_queue->prev = 0;
        head->next = 0;
        head->prev = ready_queue_tail;
        ready_queue_tail->next = head;
        ready_queue_tail = head;
    }

    process_t *next = ready_queue;
    if (next && current_process != next)
        process_switch(next);
}

void scheduler_add(process_t *proc) {
    if (!proc)
        return;

    proc->state = PROCESS_STATE_READY;

    if (!ready_queue) {
        ready_queue = proc;
        ready_queue_tail = proc;
        proc->next = 0;
        proc->prev = 0;
    } else {
        ready_queue_tail->next = proc;
        proc->prev = ready_queue_tail;
        proc->next = 0;
        ready_queue_tail = proc;
    }
}

void scheduler_remove(process_t *proc) {
    if (!proc)
        return;

    if (proc->prev)
        proc->prev->next = proc->next;
    else
        ready_queue = proc->next;

    if (proc->next)
        proc->next->prev = proc->prev;
    else
        ready_queue_tail = proc->prev;

    proc->next = 0;
    proc->prev = 0;
}

