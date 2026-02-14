#include "drivers/rtl8139.h"
#include "drivers/pci.h"
#include "util/io.h"
#include "drivers/framebuffer.h"

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RX_BUF_SIZE 8192 + 16 + 1500

// Registers
#define REG_MAC0 0x00
#define REG_MAR0 0x08
#define REG_TXSTATUS0 0x10
#define REG_TXADDR0 0x20
#define REG_RXBUF 0x30
#define REG_CMD 0x37
#define REG_CAPR 0x38 
#define REG_IMR 0x3C // Interrupt Mask Register
#define REG_ISR 0x3E // Interrupt Status Register
#define REG_TCR 0x40 // Transmit Config
#define REG_RCR 0x44 // Receive Config
#define REG_CONFIG1 0x52

uint32_t io_addr;
uint8_t rx_buffer[RX_BUF_SIZE];
uint8_t mac_addr[6];

void rtl8139_init() {
    struct PciDevice pci_dev = pci_get_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
    
    if (pci_dev.vendor_id == 0) {
        // print_str("RTL8139 not found!\n");
        return;
    }
    
    // Enable Bus Mastering
    pci_enable_bus_mastering(pci_dev);
    
    // Get IO Address (BAR0 is usually IO for RTL8139)
    // Bit 0 of BAR0 is 1 for IO space.
    io_addr = pci_dev.bar0 & (~0x3); 
    
    // Turn on
    outb(io_addr + REG_CONFIG1, 0x00);
    
    // Reset
    outb(io_addr + REG_CMD, 0x10);
    while ((inb(io_addr + REG_CMD) & 0x10) != 0) { } // Wait for reset
    
    // Init Receive Buffer
    outl(io_addr + REG_RXBUF, (uint32_t)(uint64_t)rx_buffer);
    
    // Interrupts: Enable TOK (Transmit OK) and ROK (Receive OK)
    outw(io_addr + REG_IMR, 0x0005);
    
    // Config Receive: Accept Broadcast, Multicast, Physical Match, Wrap packets
    outl(io_addr + REG_RCR, 0xAB | (1 << 7)); // (1 << 7) is WRAP
    
    // Enable RE (Receive) and TE (Transmit)
    outb(io_addr + REG_CMD, 0x0C);
    
    // Draw green square to indicate success
    framebuffer_draw_rect(0, 0, 50, 50, 0xFF00FF00);
}
