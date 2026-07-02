#include "ata.h"
#include "driver.h"
#include "arch/x86_64/cpu.h"
#include "kernel/heap.h"
#include "kernel/panic.h"

static uint32_t ata_sector_count = 0;

static void ata_wait_ready(void) {
    uint8_t status;
    do {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    } while (status & ATA_SR_BSY);
}

static void ata_wait_drq(void) {
    uint8_t status;
    do {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    } while (!(status & ATA_SR_DRQ));
}

static driver_t ata_drv = {
    .name = "ATA",
    .id = 5,
    .init = 0,  /* No init function to avoid unused warning */
    .irq = 0,
    .next = 0
};

DRIVER_REGISTER(ata_drv);

void ata_init(void) {
    /* Driver is registered via DRIVER_REGISTER macro */
}

bool ata_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count) {
    if (count == 0 || !buffer)
        return false;
    
    ata_wait_ready();
    
    /* Select drive and LBA */
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, (uint8_t)count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    
    /* Send read command */
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    /* Read data */
    for (uint32_t i = 0; i < count; i++) {
        ata_wait_drq();
        
        uint16_t *dst = (uint16_t *)(buffer + i * 512);
        for (int j = 0; j < 256; j++)
            dst[j] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }
    
    /* Check for errors */
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status & ATA_SR_ERR)
        return false;
    
    return true;
}

bool ata_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t count) {
    if (count == 0 || !buffer)
        return false;
    
    ata_wait_ready();
    
    /* Select drive and LBA */
    outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, (uint8_t)count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    
    /* Send write command */
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    /* Write data */
    for (uint32_t i = 0; i < count; i++) {
        ata_wait_drq();
        
        const uint16_t *src = (const uint16_t *)(buffer + i * 512);
        for (int j = 0; j < 256; j++)
            outw(ATA_PRIMARY_IO + ATA_REG_DATA, src[j]);
    }
    
    /* Check for errors */
    uint8_t status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status & ATA_SR_ERR)
        return false;
    
    return true;
}

uint32_t ata_get_sector_count(void) {
    return ata_sector_count;
}