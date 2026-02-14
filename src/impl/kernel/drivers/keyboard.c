#include "drivers/keyboard.h"
#include "util/io.h"
#include "cpu/isr.h"

// Interrupt-Driven Keyboard Driver

#define KEYBOARD_PORT_DATA 0x60
#define KEYBOARD_PORT_STATUS 0x64
#define RING_BUFFER_SIZE 128

#include "drivers/keyboard.h"
#include "util/io.h"
#include "cpu/isr.h"

// Interrupt-Driven Keyboard Driver with Modifiers

#define KEYBOARD_PORT_DATA 0x60
#define KEYBOARD_PORT_STATUS 0x64
#define RING_BUFFER_SIZE 128

// Internal State
static bool is_e0 = false;
static bool shift_l = false, shift_r = false;
static bool ctrl_l = false, ctrl_r = false;
static bool alt_l = false, alt_r = false;
static bool meta_l = false, meta_r = false;
static bool caps_lock = false;

// Scancode Set 1Map (Base)
static char scancode_to_char[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    KEY_CTRL, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', KEY_SHIFT,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', KEY_SHIFT, '*',
    KEY_ALT, ' ', KEY_CAPS, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_NUM, 0, // F1-F12 etc
    '-', KEY_LEFT, '5', KEY_RIGHT, '+' // Keypad
};

// Shifted Map (Symbols)
static char scancode_to_char_shift[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    KEY_CTRL, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', KEY_SHIFT,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', KEY_SHIFT, '*',
    KEY_ALT, ' ', KEY_CAPS, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, KEY_NUM, 0, 
    '-', KEY_LEFT, '5', KEY_RIGHT, '+'
};

// Circular Buffer
volatile KeyEvent event_buffer[RING_BUFFER_SIZE];
volatile int read_ptr = 0;
volatile int write_ptr = 0;

void keyboard_handler(struct registers* regs) {
    uint8_t scancode = inb(KEYBOARD_PORT_DATA);
    
    // Check for Extended Byte
    if (scancode == 0xE0) {
        is_e0 = true;
        return;
    }

    bool released = (scancode & 0x80);
    uint8_t code = scancode & 0x7F; // Strip released bit

    // 1. Handle Modifiers
    if (code == 0x2A) shift_l = !released;
    else if (code == 0x36) shift_r = !released;
    else if (code == 0x1D) {
        if (is_e0) ctrl_r = !released; 
        else ctrl_l = !released;
    }
    else if (code == 0x38) {
        if (is_e0) alt_r = !released; // AltGr
        else alt_l = !released;
    }
    else if (code == 0x3A) {
        if (!released) caps_lock = !caps_lock; // Toggle on press
    }
    else if (code == 0x5B) { // Left Windows/Command
        if (is_e0) meta_l = !released;
    }
    
    // 2. Decode Character / Special Key
    // Only process key PRESSED events for typing, but modifiers track release too.
    if (!released) {
        char c = 0;
        
        // Handle Arrows (E0 prefix)
        // Up: E0 48, Down: E0 50, Left: E0 4B, Right: E0 4D
        if (is_e0) {
             if (code == 0x48) c = KEY_UP;
             else if (code == 0x50) c = KEY_DOWN;
             else if (code == 0x4B) c = KEY_LEFT;
             else if (code == 0x4D) c = KEY_RIGHT;
        } else {
            // Normal Keys
            bool shift = (shift_l || shift_r);
            if (caps_lock) {
                // Caps affects letters only typically
                // Simple logic: if caps, invert shift behavior for letters
                // But my map handles letters + symbols in one.
                // Better: Check if letter 'a'-'z'
                char base = (code < sizeof(scancode_to_char)) ? scancode_to_char[code] : 0;
                if (base >= 'a' && base <= 'z') {
                    shift = !shift; // Invert shift for letters if Caps is on
                }
            }
            
            if (code < sizeof(scancode_to_char)) {
                if (shift) {
                    c = scancode_to_char_shift[code];
                } else {
                    c = scancode_to_char[code];
                }
            }
        }
        
        // Valid key event?
        if (c != 0 && c != KEY_SHIFT && c != KEY_CTRL && c != KEY_ALT && c != KEY_CAPS) {
            int next_write = (write_ptr + 1) % RING_BUFFER_SIZE;
            if (next_write != read_ptr) {
                KeyEvent evt;
                evt.character = c;
                evt.scancode = scancode; // Raw full scancode (maybe incomplete if E0 but ok)
                evt.released = false;
                evt.shift = (shift_l || shift_r);
                evt.ctrl = (ctrl_l || ctrl_r);
                evt.alt = (alt_l || alt_r);
                evt.caps_lock = caps_lock;
                evt.meta = (meta_l || meta_r);
                
                event_buffer[write_ptr] = evt;
                write_ptr = next_write;
            }
        }
    }

    is_e0 = false; // Consumption done
}

void keyboard_init() {
    read_ptr = 0;
    write_ptr = 0;
    while (inb(KEYBOARD_PORT_STATUS) & 1) inb(KEYBOARD_PORT_DATA);
    register_interrupt_handler(33, keyboard_handler);
}

KeyEvent keyboard_get_event() {
    if (read_ptr == write_ptr) {
        KeyEvent empty = {0};
        return empty;
    }
    
    KeyEvent evt = event_buffer[read_ptr];
    read_ptr = (read_ptr + 1) % RING_BUFFER_SIZE;
    return evt;
}

// Legacy compat
char keyboard_get_key() {
    // Peek? No, consume.
    KeyEvent evt = keyboard_get_event();
    if (evt.character > 0 && evt.character < 0x80) { // ASCII only
        return evt.character;
    }
    return 0;
}
