#include "cpu/isr.h"
#include "drivers/vga.h"
#include "cpu/pic.h"

void (*interrupt_handlers[256])(struct registers*);

void register_interrupt_handler(uint8_t n, void (*handler)(struct registers*)) {
    interrupt_handlers[n] = handler;
}

void isr_handler(struct registers* regs) {
    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handlers[regs->int_no](regs);
    } else {
        // print_str("Unhandled Exception");
    }
}

void irq_handler(struct registers* regs) {
    if (regs->int_no >= 32) {
        if (interrupt_handlers[regs->int_no] != 0) {
            interrupt_handlers[regs->int_no](regs);
        }
    }
    pic_send_eoi(regs->int_no - 32);
}
