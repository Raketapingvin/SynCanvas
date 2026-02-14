#include <drivers/audio/pc_speaker.h>
#include <util/io.h>

// PIT (Programmable Interval Timer) ports
#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43
#define PC_SPEAKER_PORT 0x61

void pc_speaker_init() {
    // Nothing special to init for PC speaker, just ensure it's off
    pc_speaker_stop_sound();
}

void pc_speaker_play_sound(uint32_t freq) {
    if (freq == 0) return;

    uint32_t div = 1193180 / freq;
    
    // Set PIT to Mode 3 (Square Wave)
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (uint8_t)(div & 0xFF));
    outb(PIT_CHANNEL2, (uint8_t)((div >> 8) & 0xFF));

    // Enable speaker (bits 0 and 1)
    uint8_t tmp = inb(PC_SPEAKER_PORT);
    if (tmp != (tmp | 3)) {
        outb(PC_SPEAKER_PORT, tmp | 3);
    }
}

void pc_speaker_stop_sound() {
    uint8_t tmp = inb(PC_SPEAKER_PORT) & 0xFC;
    outb(PC_SPEAKER_PORT, tmp);
}

// Simple delay loop since we don't have a sophisticated sleep yet
static void simple_delay(uint32_t count) {
    for (volatile uint32_t i = 0; i < count * 10000; i++) {
        __asm__ volatile ("nop");
    }
}

void pc_speaker_beep(uint32_t freq, uint32_t duration_ms) {
    pc_speaker_play_sound(freq);
    simple_delay(duration_ms);
    pc_speaker_stop_sound();
}

void pc_speaker_startup_sound() {
    // A little "Windows 95-ish" or "Intel Inside" style jingle
    // C5, E5, G5, C6 (C Major Arpeggio)
    pc_speaker_beep(523, 150); // C5
    pc_speaker_beep(659, 150); // E5
    pc_speaker_beep(784, 150); // G5
    pc_speaker_beep(1046, 300); // C6
}
