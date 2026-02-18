#include "drivers/mouse.h"
#include "util/io.h"
#include <stdbool.h>
#include "drivers/framebuffer.h"
#include "cpu/isr.h"

#define MOUSE_PORT_DATA 0x60
#define MOUSE_PORT_STATUS 0x64
#define MOUSE_CMD_ENABLE_AUX 0xA8
#define MOUSE_CMD_WRITE_AUX 0xD4
#define MOUSE_DEV_ENABLE_SCAN 0xF4

extern struct Framebuffer fb;
volatile struct MouseState mouse_state = {400, 300, 0, 0, 0}; 
volatile uint8_t mouse_cycle = 0;
volatile uint8_t mouse_byte[4];
volatile bool has_wheel = false;

void mouse_wait_write() {
    uint32_t timeout = 100000;
    while ((inb(MOUSE_PORT_STATUS) & 2) && timeout--);
}

void mouse_wait_read() {
    uint32_t timeout = 100000;
    while (!(inb(MOUSE_PORT_STATUS) & 1) && timeout--);
}

void mouse_write_dev(uint8_t write) {
    mouse_wait_write();
    outb(MOUSE_PORT_STATUS, MOUSE_CMD_WRITE_AUX);
    mouse_wait_write();
    outb(MOUSE_PORT_DATA, write);
}

uint8_t mouse_read_ack() {
    mouse_wait_read();
    return inb(MOUSE_PORT_DATA);
}

// Mouse speed: 0=slow(0.5x), 1=normal(1x), 2=fast(2x)
volatile int mouse_speed_setting = 1;

static void mouse_process_packet() {
    uint8_t flags = mouse_byte[0];
    int32_t x_rel = (int8_t) mouse_byte[1];
    int32_t y_rel = (int8_t) mouse_byte[2];

    // Apply speed multiplier
    if (mouse_speed_setting == 0) {
        x_rel = x_rel / 2;
        y_rel = y_rel / 2;
    } else if (mouse_speed_setting == 2) {
        x_rel = x_rel * 2;
        y_rel = y_rel * 2;
    }

    mouse_state.x += x_rel;
    mouse_state.y -= y_rel;

    if (has_wheel) {
        int8_t z_rel = (int8_t) mouse_byte[3];
        mouse_state.scroll_delta += z_rel;
    }

    mouse_state.left_button = flags & 1;
    mouse_state.right_button = (flags >> 1) & 1;

    // Clamp
    if (mouse_state.x < 0) mouse_state.x = 0;
    if (mouse_state.y < 0) mouse_state.y = 0;
    if (fb.width > 0 && mouse_state.x >= (int32_t)fb.width) mouse_state.x = fb.width - 1;
    if (fb.height > 0 && mouse_state.y >= (int32_t)fb.height) mouse_state.y = fb.height - 1;
}

// IRQ 12 handler - called by hardware interrupt for each mouse byte
void mouse_irq_handler(struct registers* regs) {
    uint8_t data = inb(MOUSE_PORT_DATA);

    if (mouse_cycle == 0) {
        // Byte 1: Header - Bit 3 MUST be 1 for sync
        if ((data & 0x08) == 0x08) {
            mouse_byte[0] = data;
            mouse_cycle = 1;
        }
    } else if (mouse_cycle == 1) {
        mouse_byte[1] = data;
        mouse_cycle = 2;
    } else if (mouse_cycle == 2) {
        mouse_byte[2] = data;
        if (has_wheel) {
            mouse_cycle = 3;
        } else {
            mouse_cycle = 0;
            mouse_byte[3] = 0;
            mouse_process_packet();
        }
    } else if (mouse_cycle == 3) {
        mouse_byte[3] = data;
        mouse_cycle = 0;
        mouse_process_packet();
    }
}

void mouse_enable_scroll() {
    mouse_write_dev(0xF3); mouse_read_ack();
    mouse_write_dev(200);  mouse_read_ack();

    mouse_write_dev(0xF3); mouse_read_ack();
    mouse_write_dev(100);  mouse_read_ack();

    mouse_write_dev(0xF3); mouse_read_ack();
    mouse_write_dev(80);   mouse_read_ack();
    
    mouse_write_dev(0xF2); 
    mouse_read_ack();
    uint8_t id = mouse_read_ack();
    if (id == 3 || id == 4) has_wheel = true;
}

void mouse_init() {
    // Flush any stale data from controller
    while (inb(MOUSE_PORT_STATUS) & 1) inb(MOUSE_PORT_DATA);

    // Enable Aux Device
    mouse_wait_write();
    outb(MOUSE_PORT_STATUS, MOUSE_CMD_ENABLE_AUX);
    
    // Enable IRQ 12 in 8042 controller config
    mouse_wait_write();
    outb(MOUSE_PORT_STATUS, 0x20); // Read Command Byte
    mouse_wait_read();
    uint8_t status = inb(MOUSE_PORT_DATA);
    status |= 2;   // Enable IRQ 12
    status &= ~0x20; // Make sure mouse clock is NOT disabled
    mouse_wait_write();
    outb(MOUSE_PORT_STATUS, 0x60); // Write Command Byte
    mouse_wait_write();
    outb(MOUSE_PORT_DATA, status);

    // Default settings
    mouse_write_dev(0xF6);
    mouse_read_ack();

    // Enable IntelliMouse scroll
    mouse_enable_scroll();
    
    // Enable Scanning
    mouse_write_dev(MOUSE_DEV_ENABLE_SCAN);
    mouse_read_ack();

    // Flush any data generated during init
    while (inb(MOUSE_PORT_STATUS) & 1) inb(MOUSE_PORT_DATA);
}

void mouse_enable_irq() {
    // Must be called AFTER interrupts are enabled (sti)
    // Flush stale data
    while (inb(MOUSE_PORT_STATUS) & 1) inb(MOUSE_PORT_DATA);
    
    // Reset packet state
    mouse_cycle = 0;

    // Register IRQ 12 interrupt handler (interrupt vector 44 = 32 + 12)
    register_interrupt_handler(44, mouse_irq_handler);

    // Unmask IRQ 12 at PIC (slave PIC, bit 4)
    uint8_t slave_mask = inb(0xA1);
    slave_mask &= ~(1 << 4);
    outb(0xA1, slave_mask);

    // Also ensure IRQ 2 (cascade) is unmasked on master PIC
    uint8_t master_mask = inb(0x21);
    master_mask &= ~(1 << 2);
    outb(0x21, master_mask);

    // Send EOI to clear any pending IRQ 12 from init
    outb(0xA0, 0x20); // Slave PIC EOI
    outb(0x20, 0x20); // Master PIC EOI
}

void mouse_update() {
    // No-op: mouse is now interrupt-driven
}

void mouse_handle_packet() {
    // No-op: mouse is now interrupt-driven via IRQ 12
}

struct MouseState mouse_get_state() {
    return mouse_state;
}

void mouse_clear_scroll() {
    mouse_state.scroll_delta = 0;
}
