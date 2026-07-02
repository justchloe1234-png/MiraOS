#pragma once

#include <stdint.h>

#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

#define PIC_EOI     0x20

#define PIC1_OFFSET 0x20
#define PIC2_OFFSET 0x28

void pic_init(void);
void drivers_pic_send_eoi(uint8_t irq);
void pic_send_eoi(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
uint8_t pic_read_irq(void);
