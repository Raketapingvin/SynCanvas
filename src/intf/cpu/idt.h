#pragma once
#include <stdint.h>

#define IDT_ENTRIES 256

struct IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct IdtPointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void idt_init();
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void enable_interrupts();
void disable_interrupts();
