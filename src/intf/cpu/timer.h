#pragma once
#include <stdint.h>
#include "cpu/isr.h"

void timer_init(uint32_t freq);
void timer_handler(struct registers* regs);
void sleep(uint32_t ticks);
uint64_t get_tick_count();
