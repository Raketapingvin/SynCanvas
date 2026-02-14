#pragma once
#include <stdint.h>
#include <stdbool.h>

// Extended Key Codes for non-ASCII keys
#define KEY_UP      (char)0x80
#define KEY_DOWN    (char)0x81
#define KEY_LEFT    (char)0x82
#define KEY_RIGHT   (char)0x83
#define KEY_CTRL    (char)0x84
#define KEY_ALT     (char)0x85
#define KEY_SHIFT   (char)0x86
#define KEY_META    (char)0x87 // Windows/Command
#define KEY_CAPS    (char)0x88
#define KEY_NUM     (char)0x89

typedef struct {
    char character;     // ASCII char if printable, or 0/Special Code
    uint8_t scancode;
    bool released;
    
    bool shift;
    bool caps_lock;
    bool ctrl;
    bool alt;
    bool meta;
} KeyEvent;

void keyboard_init();
KeyEvent keyboard_get_event();
// Legacy helper if needed, but we should switch to event
char keyboard_get_key(); 
