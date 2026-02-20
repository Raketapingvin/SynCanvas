#pragma once
#include <stdint.h>

// 24x24 Icons (Stored as 1 bit per pixel? Or just use raw logic? 
// Let's use a simple 24x24 array where 1=Black, 0=Transparent/White)

// House Icon (Approximate)
// 000000010000000000000000
// 000000111000000000000000
// 000001111100000000000000
// 000011111110000000000000
// 000111111111000000000000
// 001111111111100000000000
// 011111111111110000000000
// 111111111111111000000000
// 001100000000110000000000
// 001100000000110000000000
// 001100000000110000000000
// 001100111100110000000000
// 001100111100110000000000
// 001100111100110000000000
// 001111111111110000000000

// We'll proceed with procedural drawing in main.c for simplicity
// to avoid huge byte arrays in header. 

// Actually, let's define constants for layout
#define SIDEBAR_WIDTH 50
#define ICON_SIZE 32
#define ICON_PADDING 10

#define APP_HOME 0
#define APP_NOTE 1
#define APP_SETTINGS 2

// Colors
#define COL_SIDEBAR 0xFFDDDDDD // Light Grey
#define COL_BG      0xFFFFFFFF // White
#define COL_TEXT    0xFF000000 // Black
#define COL_ICON    0xFF000000 // Black
#define COL_SELECT  0xFFAAAAAA // Darker Grey
#define COL_ACCENT  0xFF4488CC // Blue accent
#define COL_TOGGLE_ON  0xFF44AA44 // Green
#define COL_TOGGLE_OFF 0xFF999999 // Grey
