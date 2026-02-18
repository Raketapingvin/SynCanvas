#include "drivers/rtl8139.h"
#include "drivers/pci.h"
#include "util/io.h"
#include "drivers/framebuffer.h"
#include "cpu/timer.h"

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RX_BUF_SIZE (8192 + 16 + 1500)

// Registers
#define REG_MAC0 0x00
#define REG_MAR0 0x08
#define REG_TXSTATUS0 0x10
#define REG_TXADDR0 0x20
#define REG_RXBUF 0x30
#define REG_CMD 0x37
#define REG_CAPR 0x38
#define REG_CBR  0x3A  // Current Buffer address (read pointer for hw)
#define REG_IMR 0x3C // Interrupt Mask Register
#define REG_ISR 0x3E // Interrupt Status Register
#define REG_TCR 0x40 // Transmit Config
#define REG_RCR 0x44 // Receive Config
#define REG_CONFIG1 0x52

static uint32_t io_addr;
static int detected = 0;
static uint8_t rx_buffer[RX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t mac_addr[6];
static uint8_t tx_buf[4][2048] __attribute__((aligned(4)));
static int cur_tx = 0;
static uint16_t rx_read_ptr = 0;

void rtl8139_init() {
    struct PciDevice pci_dev = pci_get_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID);
    
    if (pci_dev.vendor_id == 0) {
        detected = 0;
        return;
    }
    
    detected = 1;

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
    
    // Read MAC address
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = inb(io_addr + REG_MAC0 + i);
    }

    // Init TX descriptors
    cur_tx = 0;
    rx_read_ptr = 0;

    // Draw green square to indicate success
    framebuffer_draw_rect(0, 0, 50, 50, 0xFF00FF00);
}

int rtl8139_is_detected() {
    return detected;
}

void rtl8139_get_mac(uint8_t* out) {
    for (int i = 0; i < 6; i++) {
        out[i] = mac_addr[i];
    }
}

// Send a raw Ethernet frame. Returns 1 on success, 0 on failure.
int rtl8139_send_packet(void* data, uint32_t len) {
    if (!detected || len > 1792) return 0;

    // Copy packet data into current TX buffer
    uint8_t* src = (uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) {
        tx_buf[cur_tx][i] = src[i];
    }

    // Tell the NIC where the buffer is and how big
    outl(io_addr + REG_TXADDR0 + cur_tx * 4, (uint32_t)(uint64_t)tx_buf[cur_tx]);
    // Size in bits 0-12, clear OWN bit (bit 13) to start transmission
    outl(io_addr + REG_TXSTATUS0 + cur_tx * 4, len & 0x1FFF);

    // Wait for TOK (Transmit OK, bit 15) or timeout
    uint64_t start = get_tick_count();
    while (1) {
        uint32_t status = inl(io_addr + REG_TXSTATUS0 + cur_tx * 4);
        if (status & (1 << 15)) break;  // TOK - transmit OK
        if (status & (1 << 14)) break;  // TUN - transmit FIFO underrun (still sent in QEMU)
        if ((get_tick_count() - start) > 50) return 0; // 500ms timeout
    }

    cur_tx = (cur_tx + 1) % 4;
    return 1;
}

// Check if any packet has been received. Returns length or 0.
static int rtl8139_check_rx(uint8_t* out_buf, int max_len) {
    if (!detected) return 0;

    // Check if buffer is empty
    uint8_t cmd = inb(io_addr + REG_CMD);
    if (cmd & 0x01) {
        // Buffer empty bit is set, nothing to read
        // Actually bit 0 of CMD is "Buffer Empty" when read
    }

    // Check ISR for ROK
    uint16_t isr = inw(io_addr + REG_ISR);
    if (!(isr & 0x01)) return 0; // No packet received

    // Clear ROK
    outw(io_addr + REG_ISR, 0x01);

    // Read packet header from rx_buffer at rx_read_ptr
    // Format: [status(16) | length(16) | packet_data...]
    uint16_t rx_status = *(uint16_t*)(rx_buffer + rx_read_ptr);
    uint16_t rx_len    = *(uint16_t*)(rx_buffer + rx_read_ptr + 2);

    if (!(rx_status & 0x01)) return 0; // ROK bit not set in packet header

    // Sanity check length
    if (rx_len < 8 || rx_len > 1792) {
        // Bad packet, advance pointer
        rx_read_ptr = (inw(io_addr + REG_CBR) + 16) & 0x1FFF;
        outw(io_addr + REG_CAPR, rx_read_ptr - 16);
        return 0;
    }

    // Copy packet data (skip the 4-byte header)
    int copy_len = rx_len - 4; // subtract CRC
    if (copy_len > max_len) copy_len = max_len;
    for (int i = 0; i < copy_len; i++) {
        out_buf[i] = rx_buffer[(rx_read_ptr + 4 + i) % RX_BUF_SIZE];
    }

    // Advance read pointer (4-byte header + rx_len, aligned to 4 bytes + 4)
    rx_read_ptr = (rx_read_ptr + rx_len + 4 + 3) & ~3;
    rx_read_ptr %= RX_BUF_SIZE;
    outw(io_addr + REG_CAPR, rx_read_ptr - 16);

    return copy_len;
}

// Send an ARP "who-has" request to the given IP and wait for a reply.
// Returns: 1 = got ARP reply (network works), 0 = no reply (timeout)
int rtl8139_arp_ping(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3) {
    if (!detected) return 0;

    // Build ARP request packet (42 bytes)
    // Ethernet header (14) + ARP payload (28) = 42
    uint8_t pkt[64]; // pad to minimum Ethernet frame
    for (int i = 0; i < 64; i++) pkt[i] = 0;

    // --- Ethernet Header ---
    // Destination: broadcast FF:FF:FF:FF:FF:FF
    for (int i = 0; i < 6; i++) pkt[i] = 0xFF;
    // Source: our MAC
    for (int i = 0; i < 6; i++) pkt[6 + i] = mac_addr[i];
    // EtherType: ARP (0x0806)
    pkt[12] = 0x08;
    pkt[13] = 0x06;

    // --- ARP Payload ---
    // Hardware type: Ethernet (1)
    pkt[14] = 0x00; pkt[15] = 0x01;
    // Protocol type: IPv4 (0x0800)
    pkt[16] = 0x08; pkt[17] = 0x00;
    // Hardware size: 6
    pkt[18] = 6;
    // Protocol size: 4
    pkt[19] = 4;
    // Opcode: ARP Request (1)
    pkt[20] = 0x00; pkt[21] = 0x01;
    // Sender MAC
    for (int i = 0; i < 6; i++) pkt[22 + i] = mac_addr[i];
    // Sender IP: 10.0.2.15 (QEMU default guest IP)
    pkt[28] = 10; pkt[29] = 0; pkt[30] = 2; pkt[31] = 15;
    // Target MAC: 00:00:00:00:00:00 (unknown)
    for (int i = 0; i < 6; i++) pkt[32 + i] = 0x00;
    // Target IP
    pkt[38] = ip0; pkt[39] = ip1; pkt[40] = ip2; pkt[41] = ip3;

    // Clear any pending received packets / ISR
    outw(io_addr + REG_ISR, 0xFFFF);

    // Send the ARP request
    if (!rtl8139_send_packet(pkt, 64)) return 0;

    // Wait for ARP reply (up to 2 seconds = 200 ticks at 100Hz)
    uint8_t reply[256];
    uint64_t start = get_tick_count();
    while ((get_tick_count() - start) < 200) {
        int len = rtl8139_check_rx(reply, 256);
        if (len >= 42) {
            // Check if this is an ARP reply
            // EtherType at offset 12-13: 0x0806
            if (reply[12] == 0x08 && reply[13] == 0x06) {
                // ARP opcode at offset 20-21: 0x0002 = reply
                if (reply[20] == 0x00 && reply[21] == 0x02) {
                    // Check sender IP matches our target
                    if (reply[28] == ip0 && reply[29] == ip1 &&
                        reply[30] == ip2 && reply[31] == ip3) {
                        return 1; // Success! Got ARP reply
                    }
                }
            }
        }
        // Small delay before checking again
        asm volatile("hlt");
    }

    return 0; // Timeout, no reply
}
