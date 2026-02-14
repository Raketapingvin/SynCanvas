#include <drivers/rtc.h>
#include <util/io.h>

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

int get_update_in_progress_flag() {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

// BCD to Binary
uint8_t bcd2bin(uint8_t bcd) {
    return ((bcd & 0xF0) >> 1) + ( (bcd & 0xF0) >> 3) + (bcd & 0x0f);
}

Time rtc_get_time() {
    Time t;
    
    // Wait until update is not in progress
    // (In a real robust OS we read twice to confirm consistency)
    while (get_update_in_progress_flag());

    t.seconds = get_rtc_register(0x00);
    t.minutes = get_rtc_register(0x02);
    t.hours   = get_rtc_register(0x04);
    t.day     = get_rtc_register(0x07);
    t.month   = get_rtc_register(0x08);
    t.year    = get_rtc_register(0x09);
    
    // Check Status Register B for BCD mode
    uint8_t registerB = get_rtc_register(0x0B);

    if (!(registerB & 0x04)) {
        t.seconds = bcd2bin(t.seconds);
        t.minutes = bcd2bin(t.minutes);
        t.hours   = bcd2bin(t.hours);
        t.day     = bcd2bin(t.day);
        t.month   = bcd2bin(t.month);
        t.year    = bcd2bin(t.year);
    }

    t.century = 20; // Assumption for now
    return t;
}
