#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/framebuffer.h"
#include "cpu/idt.h"
#include "drivers/rtl8139.h"
#include "drivers/audio/pc_speaker.h"
#include "drivers/audio/hda.h"
#include "drivers/audio/ac97.h"
#include "drivers/wireless/bluetooth.h"
#include "drivers/storage/ata.h"
#include "drivers/storage/ahci.h"
#include "drivers/storage/nvme.h"
#include "drivers/usb/uhci.h"
#include "drivers/usb/xhci.h"
#include "drivers/display/graphics.h"
#include "drivers/display/text.h"
#include "cpu/timer.h" 
#include "drivers/rtc.h"
#include "ui/ui.h"

extern struct Framebuffer fb;

// --- GUI STATE ---
int current_app = APP_HOME;
char notepad_buffer[1024];
int notepad_cursor = 0;
bool request_redraw = true;

// --- THEME SYSTEM ---
int current_theme = THEME_DEFAULT;

// Theme color definitions
uint32_t get_col_sidebar(void) {
    switch (current_theme) {
        case THEME_GREEN:  return 0xFFC8E6C9; // Light Green
        case THEME_ORANGE: return 0xFFFFE0B2; // Light Orange
        case THEME_YELLOW: return 0xFFFFF9C4; // Light Yellow
        case THEME_BLUE:   return 0xFFBBDEFB; // Light Blue
        default:           return 0xFFDDDDDD; // Light Grey
    }
}

uint32_t get_col_bg(void) {
    switch (current_theme) {
        case THEME_GREEN:  return 0xFFF1F8E9; // Very Light Green
        case THEME_ORANGE: return 0xFFFFF3E0; // Very Light Orange
        case THEME_YELLOW: return 0xFFFFFDE7; // Very Light Yellow
        case THEME_BLUE:   return 0xFFE3F2FD; // Very Light Blue
        default:           return 0xFFFFFFFF; // White
    }
}

uint32_t get_col_text(void) {
    return 0xFF000000; // Black text for all themes
}

uint32_t get_col_icon(void) {
    switch (current_theme) {
        case THEME_GREEN:  return 0xFF2E7D32; // Dark Green
        case THEME_ORANGE: return 0xFFE65100; // Dark Orange
        case THEME_YELLOW: return 0xFFF57F17; // Dark Yellow
        case THEME_BLUE:   return 0xFF1565C0; // Dark Blue
        default:           return 0xFF000000; // Black
    }
}

uint32_t get_col_select(void) {
    switch (current_theme) {
        case THEME_GREEN:  return 0xFFA5D6A7; // Medium Green
        case THEME_ORANGE: return 0xFFFFCC80; // Medium Orange
        case THEME_YELLOW: return 0xFFFFF59D; // Medium Yellow
        case THEME_BLUE:   return 0xFF90CAF9; // Medium Blue
        default:           return 0xFFAAAAAA; // Darker Grey
    }
}

// Cursor dimensions must match framebuffer_draw_cursor
#define CURSOR_W 12
#define CURSOR_H 16

// --- HELPERS ---

void print_2digits(int val, int x, int y, int scale) {
    char buf[3];
    buf[0] = '0' + (val / 10);
    buf[1] = '0' + (val % 10);
    buf[2] = 0;
    text_draw_string_scaled(buf, x, y, COL_TEXT, COL_BG, scale);
}

void print_4digits(int val, int x, int y, int scale) {
    // Basic year printer (2026)
    char buf[5];
    buf[0] = '0' + (val / 1000);
    buf[1] = '0' + ((val / 100) % 10);
    buf[2] = '0' + ((val / 10) % 10);
    buf[3] = '0' + (val % 10);
    buf[4] = 0;
    text_draw_string_scaled(buf, x, y, COL_TEXT, COL_BG, scale);
}


// Check click hits
int get_clicked_app(int x, int y) {
    if (x < SIDEBAR_WIDTH) {
        if (y > 50 && y < 90) return APP_HOME;
        if (y > 100 && y < 140) return APP_NOTE;
    }
    return -1;
}

void draw_sidebar() {
    // 1. Draw Sidebar Background
    framebuffer_draw_rect(0, 0, SIDEBAR_WIDTH, 768, COL_SIDEBAR); 
    
    // 2. Draw Divider Line
    framebuffer_draw_rect(SIDEBAR_WIDTH, 0, 2, 768, 0xFF999999);

    // 3. Draw Selection Highlight
    if (current_app == APP_HOME) {
        framebuffer_draw_rect(5, 50, 40, 40, COL_SELECT);
    } else if (current_app == APP_NOTE) {
        framebuffer_draw_rect(5, 100, 40, 40, COL_SELECT);
    }

    // 4. Draw "House" Icon (at y=50) 
    // Roof
    for(int i=0; i<20; i++) {
         framebuffer_draw_rect(25-i, 50+10+i, i*2, 1, COL_ICON);
    }
    // Base
    framebuffer_draw_rect(15, 70+5, 20, 15, COL_ICON);
    // Door
    framebuffer_draw_rect(22, 75+5, 6, 10, 0xFFDDDDDD); 

    // 5. Draw "Note" Icon (at y=100)
    framebuffer_draw_rect(15, 105, 20, 30, COL_ICON); // Paper shape
    framebuffer_draw_rect(17, 107, 16, 26, 0xFFFFFFFF); // White content
    // Lines
    framebuffer_draw_rect(19, 112, 12, 1, COL_ICON);
    framebuffer_draw_rect(19, 116, 12, 1, COL_ICON);
    framebuffer_draw_rect(19, 120, 12, 1, COL_ICON);
}

void draw_content() {
    // Clear Content Area (White)
    framebuffer_draw_rect(SIDEBAR_WIDTH + 2, 0, 800 - SIDEBAR_WIDTH, 600, COL_BG);

    if (current_app == APP_HOME) {
        text_draw_string_scaled("Welcome to SynCanvas", SIDEBAR_WIDTH + 20, 50, COL_TEXT, COL_BG, 2);
        
        Time t = rtc_get_time();
        int base_y = 90;
        int scale = 3;
        int char_w = 8 * scale;
        
        text_draw_string_scaled("Time: ", SIDEBAR_WIDTH + 20, base_y, COL_TEXT, COL_BG, scale);
        int x_off = SIDEBAR_WIDTH + 20 + (6 * char_w);
        print_2digits(t.hours, x_off, base_y, scale);
        
        x_off += (2 * char_w);
        text_draw_string_scaled(":", x_off, base_y, COL_TEXT, COL_BG, scale);
        
        x_off += char_w;
        print_2digits(t.minutes, x_off, base_y, scale);

        base_y += (10 * scale); // New line
        text_draw_string_scaled("Date: ", SIDEBAR_WIDTH + 20, base_y, COL_TEXT, COL_BG, scale);
        
        x_off = SIDEBAR_WIDTH + 20 + (6 * char_w);
        print_2digits(t.day, x_off, base_y, scale);
        
        x_off += (2 * char_w);
        text_draw_string_scaled("/", x_off, base_y, COL_TEXT, COL_BG, scale);
        
        x_off += char_w;
        print_2digits(t.month, x_off, base_y, scale);
        
        x_off += (2 * char_w);
        text_draw_string_scaled("/", x_off, base_y, COL_TEXT, COL_BG, scale);
        
        x_off += char_w;
        // Correct year handling depending on century
        print_4digits(2000 + t.year, x_off, base_y, scale);

        // Theme indicator
        base_y += (15 * scale);
        text_draw_string("Press 'T' to change theme", SIDEBAR_WIDTH + 20, base_y, COL_TEXT, COL_BG);
        base_y += 20;
        
        const char* theme_names[] = {"Default", "Green", "Orange", "Yellow", "Blue"};
        text_draw_string("Current theme: ", SIDEBAR_WIDTH + 20, base_y, COL_TEXT, COL_BG);
        text_draw_string(theme_names[current_theme], SIDEBAR_WIDTH + 20 + 16*8, base_y, COL_ICON, COL_BG);

    } else if (current_app == APP_NOTE) {
        text_draw_string("Notepad", SIDEBAR_WIDTH + 20, 20, COL_TEXT, COL_BG);
        text_draw_string("Type using keyboard...", SIDEBAR_WIDTH + 20, 40, 0xFFAAAAAA, COL_BG);
        
        // Draw Buffer with auto line wrapping
        int max_chars_per_line = (fb.width - SIDEBAR_WIDTH - 40) / 8;
        if (max_chars_per_line < 1) max_chars_per_line = 1;
        int cur_x = SIDEBAR_WIDTH + 20;
        int cur_y = 60;
        int col_idx = 0;
        for (int i = 0; i < notepad_cursor; i++) {
            text_draw_char(notepad_buffer[i], cur_x, cur_y, COL_TEXT, COL_BG);
            cur_x += 8;
            col_idx++;
            if (col_idx >= max_chars_per_line) {
                col_idx = 0;
                cur_x = SIDEBAR_WIDTH + 20;
                cur_y += 10;
            }
        }
        
        // Cursor
        framebuffer_draw_rect(cur_x, cur_y, 2, 8, COL_TEXT);
    }
}


void kernel_main(unsigned long addr) {
    // Stage 1: Init Core
    idt_init();
    timer_init(100);
    
    // Stage 2: Graphics
    framebuffer_init((void*)addr);
    framebuffer_clear(COL_BG); // White Background

    // Stage 3: Drivers
    mouse_init();
    keyboard_init();
    
    // Init others silently (to ensure detection works if we query later)
    ata_init(); ahci_init(); nvme_init();
    usbus_init(); xhci_init();
    rtl8139_init(); 
    
    // Clear screen AGAIN after drivers init to hide boot logs/boxes and show clean GUI.
    framebuffer_clear(COL_BG);
    framebuffer_swap();
    
    // Enable Interrupts
    asm volatile("sti");

    // Enable mouse IRQ handler (must be after sti)
    mouse_enable_irq();

    // Initial GUI Draw
    draw_sidebar();
    draw_content();

    // Mouse Logic
    struct MouseState last_mouse = {0,0,0,0};
    uint32_t cursor_backing_store[CURSOR_W * CURSOR_H]; 
    
    // Initial Bg Save
    struct MouseState temp = mouse_get_state();
    last_mouse = temp;
    for(int y=0; y<CURSOR_H; y++) {
        for(int x=0; x<CURSOR_W; x++) {
            cursor_backing_store[y*CURSOR_W + x] = framebuffer_get_pixel(last_mouse.x + x, last_mouse.y + y);
        }
    }
    framebuffer_draw_cursor(last_mouse.x, last_mouse.y);
    framebuffer_swap();

    request_redraw = true;

    while (1) {
        mouse_handle_packet(); 

        struct MouseState current = mouse_get_state();
        char key = keyboard_get_key();

        bool screen_dirty = false;

        // Input: Keyboard (Notepad)
        if (key > 0) {
            // Theme switching with T key (works in any app)
            if (key == 't' || key == 'T') {
                current_theme = (current_theme + 1) % 5; // Cycle through 5 themes
                screen_dirty = true;
            }
            else if (current_app == APP_NOTE) {
                if (key == '\b') { // Backspace
                    if (notepad_cursor > 0) {
                        notepad_cursor--;
                        notepad_buffer[notepad_cursor] = 0;
                        screen_dirty = true;
                    }
                } else if (key == '\n') {
                    // Ignore newlines for simple single-line demo or handle later
                } else {
                    if (notepad_cursor < 1000) {
                        notepad_buffer[notepad_cursor] = key;
                        notepad_cursor++;
                        notepad_buffer[notepad_cursor] = 0; // null term
                        screen_dirty = true;
                    }
                }
            }
        }

        // Input: Mouse Click
        if (current.left_button && !last_mouse.left_button) { // Clicked just now
             int clicked = get_clicked_app(current.x, current.y);
             if (clicked != -1 && clicked != current_app) {
                 current_app = clicked;
                 screen_dirty = true;
             }
        }

        // Redraw GUI if needed (before Mouse Cursor)
        if (screen_dirty || request_redraw) {
            // Restore BG under cursor first (effectively hiding it)
            for(int y=0; y<CURSOR_H; y++) {
                for(int x=0; x<CURSOR_W; x++) {
                     framebuffer_put_pixel(last_mouse.x + x, last_mouse.y + y, cursor_backing_store[y*CURSOR_W + x]);
                }
            }

            // Redraw clean UI
            draw_sidebar(); // Reselect highlight
            draw_content(); // New content

            // Updates Backing Store for Current Pos (which is now clean UI)
            for(int y=0; y<CURSOR_H; y++) {
                for(int x=0; x<CURSOR_W; x++) {
                    cursor_backing_store[y*CURSOR_W + x] = framebuffer_get_pixel(last_mouse.x + x, last_mouse.y + y);
                }
            }
            // Redraw Cursor
            framebuffer_draw_cursor(last_mouse.x, last_mouse.y);
            framebuffer_swap();
            
            request_redraw = false;
        }

        // Mouse Movement
        if (current.x != last_mouse.x || current.y != last_mouse.y) {
            // Restore old cursor area in back buffer
            for(int y=0; y<CURSOR_H; y++) {
                for(int x=0; x<CURSOR_W; x++) {
                    framebuffer_put_pixel(last_mouse.x + x, last_mouse.y + y, cursor_backing_store[y*CURSOR_W + x]);
                }
            }
            // Blit restored area to screen
            framebuffer_blit_rect(last_mouse.x, last_mouse.y, CURSOR_W, CURSOR_H);

            // Save new background
            for(int y=0; y<CURSOR_H; y++) {
                for(int x=0; x<CURSOR_W; x++) {
                    cursor_backing_store[y*CURSOR_W + x] = framebuffer_get_pixel(current.x + x, current.y + y);
                }
            }
            // Draw cursor in back buffer and blit to screen
            framebuffer_draw_cursor(current.x, current.y);
            framebuffer_blit_rect(current.x, current.y, CURSOR_W, CURSOR_H);
            last_mouse = current;
        }
        
        asm volatile("hlt");
    }
}
