#include "cpu/pic.h"
#include "util/io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_ICW4       0x01
#define ICW1_INIT       0x10
#define ICW4_8086       0x01

void pic_remap() {
    uint8_t a1, a2;

    a1 = inb(PIC1_DATA);                        // Save masks
    a2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);  // Start init sequence
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    outb(PIC1_DATA, 0x20);                      // Vector offset for Master PIC (32)
    outb(PIC2_DATA, 0x28);                      // Vector offset for Slave PIC (40)

    outb(PIC1_DATA, 4);                         // Tell master about slave at IRQ2
    outb(PIC2_DATA, 2);                         // Tell slave its identity

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    outb(PIC1_DATA, a1);                        // Restore masks
    outb(PIC2_DATA, a2);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_COMMAND, 0x20);
    outb(PIC1_COMMAND, 0x20);
}
