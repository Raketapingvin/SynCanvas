#include <drivers/storage/ata.h>
#include <util/io.h>

#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERR         0x1F1
#define ATA_PRIMARY_SEC_COUNT   0x1F2
#define ATA_PRIMARY_LBA_LO      0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HI      0x1F5
#define ATA_PRIMARY_DRIVE_HEAD  0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7

#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_IDENTIFY        0xEC

void ata_wait_bsy() {
    while(inb(ATA_PRIMARY_STATUS) & 0x80);
}

void ata_wait_drq() {
    while(!(inb(ATA_PRIMARY_STATUS) & 0x08));
}

int ata_init() {
    // Select master drive
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xA0);
    
    // Set sector count/LBA to 0
    outb(ATA_PRIMARY_SEC_COUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);
    
    // Send identify command
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    
    if(status == 0) return 0; // No drive
    
    // Wait until BSY clears
    ata_wait_bsy();
    
    // Read STATUS again
    status = inb(ATA_PRIMARY_STATUS);
    // If ERR (bit 0) is set, clean up and return
    if(status & 1) return 0;
    
    // Wait for DRQ
    ata_wait_drq();
    
    // Read identification data (256 words)
    for(int i = 0; i < 256; i++) {
        uint16_t tmp = inw(ATA_PRIMARY_DATA);
        (void)tmp; // Discard for now
    }
    
    return 1; // Success
}

void ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    ata_wait_bsy();
    
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SEC_COUNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_PIO);
    
    for (int i = 0; i < count; i++) {
        ata_wait_bsy();
        ata_wait_drq();
        
        for (int j = 0; j < 256; j++) {
            uint16_t data = inw(ATA_PRIMARY_DATA);
            // Write 2 bytes
            buffer[j * 2] = (uint8_t)data;
            buffer[j * 2 + 1] = (uint8_t)(data >> 8);
        }
        buffer += 512;
    }
}

void ata_write_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    ata_wait_bsy();
    
    outb(ATA_PRIMARY_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SEC_COUNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_PIO);
    
    for (int i = 0; i < count; i++) {
        ata_wait_bsy();
        ata_wait_drq();
        
        for (int j = 0; j < 256; j++) {
            uint16_t data = buffer[j * 2] | (buffer[j * 2 + 1] << 8);
            outw(ATA_PRIMARY_DATA, data);
        }
        buffer += 512;
    }
}
