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

// Theme system
#define THEME_DEFAULT 0
#define THEME_GREEN   1
#define THEME_ORANGE  2
#define THEME_YELLOW  3
#define THEME_BLUE    4

// Current theme (global variable, defined in main.c)
extern int current_theme;

// Theme-aware color getters
uint32_t get_col_sidebar(void);
uint32_t get_col_bg(void);
uint32_t get_col_text(void);
uint32_t get_col_icon(void);
uint32_t get_col_select(void);

// Legacy color definitions (for backwards compatibility)
#define COL_SIDEBAR get_col_sidebar()
#define COL_BG      get_col_bg()
#define COL_TEXT    get_col_text()
#define COL_ICON    get_col_icon()
#define COL_SELECT  get_col_select()
