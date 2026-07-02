#include "timer.h"
#include "driver.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/isr.h"

static volatile uint64_t tick_count;

static int timer_driver_init(void) {
    timer_init();
    return 0;
}

static void timer_driver_irq(uint8_t irq) {
    (void)irq;
    timer_on_irq();
    
    /* Drive process scheduling */
    extern void scheduler_tick(void);
    scheduler_tick();
}


static driver_t timer_driver = {
    .name = "pit",
    .id = DRIVER_ID_TIMER,
    .init = timer_driver_init,
    .irq = timer_driver_irq,
    .next = 0
};

DRIVER_REGISTER(timer_driver);

void timer_init(void) {
    tick_count = 0;
    uint32_t divisor = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

void timer_on_irq(void) {
    tick_count++;
}

uint64_t timer_ticks(void) {
    return tick_count;
}
