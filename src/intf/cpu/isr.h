#pragma once
#include <stdint.h>

struct registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

void isr_handler(struct registers* regs);
void irq_handler(struct registers* regs);
void register_interrupt_handler(uint8_t n, void (*handler)(struct registers*));
