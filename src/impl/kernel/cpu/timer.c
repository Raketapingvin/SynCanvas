#include "cpu/timer.h"
#include "cpu/isr.h"
#include "util/io.h"
#include "drivers/display/text.h" // Debug print

volatile uint64_t ticks = 0;

void timer_handler(struct registers* regs) {
    ticks++;
    // Optional: Print a dot every second (100 ticks)
    // if (ticks % 100 == 0) {
    //     // debug feedback
    // }
}

// 0x43 is command, 0x40 is channel 0
// Square wave mode (3) 
void timer_init(uint32_t freq) {
    // Install the handler
    register_interrupt_handler(32, timer_handler);

    // Get the PIT value: hardware clock at 1193180 Hz
    uint32_t divisor = 1193180 / freq;

    // Send the command byte.
    // 00 (Channel 0) | 11 (Access LOByte/HIByte) | 011 (Square wave) | 0 (Binary mode)
    // 0x36
    outb(0x43, 0x36);
    
    // Split divisor into bytes
    uint8_t l = (uint8_t)(divisor & 0xFF);
    uint8_t h = (uint8_t)( (divisor>>8) & 0xFF );

    // Send the frequency divisor.
    outb(0x40, l);
    outb(0x40, h);
}

void sleep(uint32_t ms) {
    // We are running at 100Hz = 10ms per tick.
    // So ticks needed = ms / 10.
    // If we want accurate sleep, we need higher freq, but user wants optimization.
    // 100Hz is efficient.
    
    uint64_t ticks_to_wait = ms / 10;
    if (ticks_to_wait == 0) ticks_to_wait = 1; // Minimum 1 tick

    uint64_t end_ticks = ticks + ticks_to_wait;
    
    while(ticks < end_ticks) {
        // Halt CPU until next interrupt (Power saving!)
        // "hlt" instruction
        asm volatile("hlt");
    }
}

uint64_t get_tick_count() {
    return ticks;
}
