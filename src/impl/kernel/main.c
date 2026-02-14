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

// --- GUI STATE ---
int current_app = APP_HOME;
// Increased buffer size to 64KB for "no limit" feel
char notepad_buffer[65536]; 
int notepad_cursor = 0;
int selection_anchor = -1; // -1 means no selection
int notepad_scroll_y = 0; // Vertical scroll offset in lines
bool request_redraw = true;

// Internal Helpers for Strings
int str_len(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

void mem_move(void* dest, const void* src, int n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    if (d == s) return;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
}

// Notepad Logic
void delete_selection() {
    if (selection_anchor == -1 || selection_anchor == notepad_cursor) {
        selection_anchor = -1;
        return;
    }
    
    int start = (selection_anchor < notepad_cursor) ? selection_anchor : notepad_cursor;
    int end = (selection_anchor < notepad_cursor) ? notepad_cursor : selection_anchor;
    int len = str_len(notepad_buffer);
    
    // Move tail backwards
    mem_move(&notepad_buffer[start], &notepad_buffer[end], len - end + 1); // +1 for null
    
    notepad_cursor = start;
    selection_anchor = -1;
}

// Helper to get visual line/col for a cursor index
void get_visual_pos(int cursor_idx, int* out_line, int* out_col) {
    int start_x = 0; // normalized
    int max_x = 800 - 30; // 20 pad left + 10 right
    int cur_x = 0;
    int cur_line = 0;
    
    for (int i = 0; i < 65535; i++) {
        if (i == cursor_idx) {
            *out_line = cur_line;
            *out_col = cur_x / 8;
            return;
        }
        char c = notepad_buffer[i];
        if (c == 0) break;
        
        if (c == '\n') {
            cur_x = 0;
            cur_line++;
            continue;
        }
        
        if (cur_x + 8 >= max_x) {
            cur_x = 0;
            cur_line++;
        }
        cur_x += 8;
    }
    // If not found (end), return current
    *out_line = cur_line;
    *out_col = cur_x / 8;
}

// Helper to find cursor index from visual line/col
int get_index_from_visual(int target_line, int target_col) {
    int start_x = 0;
    int max_x = 800 - 30;
    int cur_x = 0;
    int cur_line = 0;
    int last_valid_i = 0;
    
    for (int i = 0; i < 65535; i++) {
        // Check if we found the line
        if (cur_line == target_line) {
            // Check if we reached the column OR end of line
            if ((cur_x / 8) >= target_col) return i;
        }
        // If we passed the line
        if (cur_line > target_line) return i - 1; // End of prev line was best match
        
        last_valid_i = i;
        char c = notepad_buffer[i];
        if (c == 0) return i;
        
        if (c == '\n') {
            if (cur_line == target_line) return i; // Return index of newline if we didn't reach col
            cur_x = 0;
            cur_line++;
            continue;
        }
        
        if (cur_x + 8 >= max_x) {
             if (cur_line == target_line) return i; // End of visual line
             cur_x = 0;
             cur_line++;
        }
        cur_x += 8;
    }
    return last_valid_i;
}


void insert_char(char c) {
    if (selection_anchor != -1) delete_selection();
    
    int len = str_len(notepad_buffer);
    if (len >= 65534) return; // Leave space for null
    
    // Check if we are inserting at end (optimization)
    if (notepad_cursor == len) {
        notepad_buffer[notepad_cursor] = c;
        notepad_buffer[notepad_cursor+1] = 0;
    } else {
        // Shift right, ensure we move the null terminator too
        // len - cursor + 1 is the count of chars to move including null
        mem_move(&notepad_buffer[notepad_cursor + 1], &notepad_buffer[notepad_cursor], len - notepad_cursor + 1);
        notepad_buffer[notepad_cursor] = c;
    }
    notepad_cursor++;
    request_redraw = true; 
}

void delete_char_back() {
    if (selection_anchor != -1) {
        delete_selection();
        return;
    }
    
    if (notepad_cursor > 0) {
        int len = str_len(notepad_buffer);
        mem_move(&notepad_buffer[notepad_cursor - 1], &notepad_buffer[notepad_cursor], len - notepad_cursor + 1);
        notepad_cursor--;
    }
}

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
    static int last_rendered_app_sidebar = -1;
    if (current_app == last_rendered_app_sidebar && !request_redraw) return;
    last_rendered_app_sidebar = current_app;

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

void draw_content(int min_dirty_index) {
    static int last_rendered_app = -1;
    bool app_changed = (current_app != last_rendered_app);

    // Force full redraw if app changed or request is 'full' (min_dirty_index < 0)
    if (app_changed || min_dirty_index < 0) {
        if (app_changed) {
            framebuffer_draw_rect(SIDEBAR_WIDTH + 2, 0, 800 - SIDEBAR_WIDTH, 600, COL_BG);
            last_rendered_app = current_app;
        }
    }

    if (current_app == APP_HOME) {
        // Redraw home only if needed
        if (app_changed || min_dirty_index < 0) {
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
        }


    } else if (current_app == APP_NOTE) {
        if (app_changed) {
            text_draw_string("Notepad", SIDEBAR_WIDTH + 20, 20, COL_TEXT, COL_BG);
            char buf[32];
            // Show buffer usage
            int len = str_len(notepad_buffer);
            text_draw_string("Size: ", SIDEBAR_WIDTH + 20, 40, 0xFFAAAAAA, COL_BG);
            print_4digits(len, SIDEBAR_WIDTH + 70, 40, 1);
            
            framebuffer_draw_rect(SIDEBAR_WIDTH + 20, 60, 800 - SIDEBAR_WIDTH - 20, 540, COL_BG);
        }
        
        // Multi-line Text Drawing with Wrapping & Scrolling
        int start_x = SIDEBAR_WIDTH + 20;
        int max_x = 800 - 10;
        int line_height = 10;
        int max_visible_lines = 50; // 500px / 10px
        
        int cur_x = start_x;
        int cur_line = 0; // Logical visual line index
        
        // Calculate visual cursor position first to adjust scroll
        int visual_cursor_line = -1;
        int visual_cursor_x = -1;
        
        // Pass 1: Layout only (to find cursor line)
        {
            int t_x = start_x;
            int t_line = 0;
            for (int i = 0; i <= 65535; i++) {
                if (i == notepad_cursor) {
                    visual_cursor_line = t_line;
                    visual_cursor_x = t_x;
                    break; 
                }
                char c = notepad_buffer[i];
                if (c == 0) break;
                
                if (c == '\n') {
                    t_x = start_x;
                    t_line++;
                    continue;
                }
                if (t_x + 8 >= max_x) {
                    t_x = start_x;
                    t_line++;
                }
                t_x += 8;
            }
             // Edge case: cursor at very end
            if (visual_cursor_line == -1) {
                 visual_cursor_line = t_line; 
            }
        }
        
        // Auto-scroll logic
        if (visual_cursor_line < notepad_scroll_y) {
            notepad_scroll_y = visual_cursor_line;
            request_redraw = true; // Force redraw with new scroll
        } else if (visual_cursor_line >= notepad_scroll_y + max_visible_lines) {
            notepad_scroll_y = visual_cursor_line - max_visible_lines + 1;
            request_redraw = true;
        }

        // Pass 2: Drawing
        cur_x = start_x;
        cur_line = 0;
        
        // Track cursor screen position for drawing after text
        int cursor_draw_x = -1;
        int cursor_draw_y = -1;
        
        for (int i = 0; i < 65535; i++) { // Render loop
             char c = notepad_buffer[i];
             // Check if we stop
             if (c == 0 && i != notepad_cursor) break; // Allow drawing cursor at end
             
             // Setup pos
             int draw_y = 60 + (cur_line - notepad_scroll_y) * line_height;
             
             // Save cursor position (draw it after all text)
             if (i == notepad_cursor) {
                 if (cur_line >= notepad_scroll_y && cur_line < notepad_scroll_y + max_visible_lines) {
                     cursor_draw_x = cur_x;
                     cursor_draw_y = draw_y;
                 }
             }
             
             if (c == 0) break; // Finished buffer

             // Newline
             if (c == '\n') {
                 // Clear rest of line only if visible
                 if (cur_line >= notepad_scroll_y && cur_line < notepad_scroll_y + max_visible_lines) {
                      if (cur_x < max_x) framebuffer_draw_rect(cur_x, draw_y, max_x - cur_x, line_height, COL_BG); 
                 }
                 cur_x = start_x;
                 cur_line++;
                 continue;
             }
             
             // Wrap
             if (cur_x + 8 >= max_x) {
                 // Clear any tiny remainder to max_x? Maybe just 1-2 pixels
                 if (cur_line >= notepad_scroll_y && cur_line < notepad_scroll_y + max_visible_lines) {
                      if (cur_x < max_x) framebuffer_draw_rect(cur_x, draw_y, max_x - cur_x, line_height, COL_BG); 
                 }

                 cur_x = start_x;
                 cur_line++;
                 draw_y = 60 + (cur_line - notepad_scroll_y) * line_height;
             }

             // Draw Char if visible
             if (cur_line >= notepad_scroll_y && cur_line < notepad_scroll_y + max_visible_lines) {
                 bool selected = false;
                 if (selection_anchor != -1) {
                     int s = (selection_anchor < notepad_cursor) ? selection_anchor : notepad_cursor;
                     int e = (selection_anchor < notepad_cursor) ? notepad_cursor : selection_anchor;
                     if (i >= s && i < e) selected = true;
                 }
                 uint32_t bg = selected ? COL_SELECT : COL_BG;
                 uint32_t fg = selected ? 0xFFFFFFFF : COL_TEXT;
                 // Clear full cell (including gap below font) to remove cursor remnants
                 framebuffer_draw_rect(cur_x, draw_y, 8, line_height, bg);
                 text_draw_char(c, cur_x, draw_y, fg, bg);
             }
             
             cur_x += 8;
        }
        
        // CLEAR REMAINDER OF CURRENT LINE (Fixes ghost chars on backspace)
        int final_draw_y = 60 + (cur_line - notepad_scroll_y) * line_height;
        if (cur_line >= notepad_scroll_y && cur_line < notepad_scroll_y + max_visible_lines) {
            if (cur_x < max_x) {
                framebuffer_draw_rect(cur_x, final_draw_y, max_x - cur_x, line_height, COL_BG);
            }
        }
        
        // Clear bottom
        int clean_y = 60 + (cur_line - notepad_scroll_y + 1) * line_height;
        if (clean_y < 600) {
            framebuffer_draw_rect(start_x, clean_y, max_x - start_x, 600 - clean_y, COL_BG); 
        }
        
        // Draw text cursor AFTER all text so it's always visible
        if (cursor_draw_x >= 0 && cursor_draw_y >= 0) {
            framebuffer_draw_rect(cursor_draw_x, cursor_draw_y, 2, 8, COL_TEXT);
        }
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
    
    // Enable Interrupts
    asm volatile("sti");

    // Now that interrupts are enabled, activate mouse IRQ handler
    mouse_enable_irq();

    // Initial GUI Draw
    draw_sidebar();
    draw_content(-1);

    // Mouse Logic
    struct MouseState last_mouse = {0,0,0,0,0};
    uint32_t cursor_backing_store[12*16]; 
    
    // Initial Bg Save
    struct MouseState temp = mouse_get_state();
    last_mouse = temp;
    for(int y=0; y<16; y++) {
        for(int x=0; x<12; x++) {
            cursor_backing_store[y*12 + x] = framebuffer_get_pixel(last_mouse.x + x, last_mouse.y + y);
        }
    }
    framebuffer_draw_cursor(last_mouse.x, last_mouse.y);

    request_redraw = true;

    while (1) {
        // Mouse is now interrupt-driven (IRQ 12), no polling needed

        struct MouseState current = mouse_get_state();
        KeyEvent key_evt;
        bool screen_dirty = false;
        
        // Track the earliest change to update only necessary parts
        int min_dirty_index = -1;

        // Input: Keyboard (Notepad) Use Event System
        static char system_clipboard[4096] = {0};

        while (1) {
            key_evt = keyboard_get_event();
            if (key_evt.character == 0 && key_evt.scancode == 0) break; // Empty

            if (current_app == APP_NOTE) {
                // Determine Action
                bool is_move = (key_evt.character == KEY_UP || key_evt.character == KEY_DOWN || 
                                key_evt.character == KEY_LEFT || key_evt.character == KEY_RIGHT);
                
                // Movement
                if (is_move) {
                    if (key_evt.shift) {
                        if (selection_anchor == -1) selection_anchor = notepad_cursor;
                    } else {
                        selection_anchor = -1; // Reset selection if not shifting
                    }
                    
                    if (key_evt.character == KEY_LEFT) {
                        if (key_evt.ctrl || key_evt.meta) {
                             // Jump Word Left
                             if (notepad_cursor > 0) {
                                  // 1. Skip trailing spaces
                                  while(notepad_cursor > 0 && notepad_buffer[notepad_cursor-1] == ' ') notepad_cursor--;
                                  // 2. Skip word characters
                                  while(notepad_cursor > 0 && notepad_buffer[notepad_cursor-1] != ' ') notepad_cursor--;
                             }
                        } else {
                             if (notepad_cursor > 0) notepad_cursor--;
                        }
                    } 
                    else if (key_evt.character == KEY_RIGHT) {
                        if (key_evt.ctrl || key_evt.meta) {
                             // Jump Word Right
                             int len = str_len(notepad_buffer);
                             if (notepad_cursor < len) {
                                  // 1. Skip word characers
                                  while(notepad_cursor < len && notepad_buffer[notepad_cursor] != ' ') notepad_cursor++;
                                  // 2. Skip separating spaces
                                  while(notepad_cursor < len && notepad_buffer[notepad_cursor] == ' ') notepad_cursor++;
                             }
                        } else {
                             if (notepad_buffer[notepad_cursor] != 0) notepad_cursor++;
                        }
                    }
                    else if (key_evt.character == KEY_UP) {
                        // Simple Line-Based Navigation (Reverted)
                        // 1. Find start of current line (search backwards for newline)
                        int curr_line_start = notepad_cursor;
                        while(curr_line_start > 0 && notepad_buffer[curr_line_start-1] != '\n') curr_line_start--;
                        
                        int col_curr = notepad_cursor - curr_line_start; // Logical column
                        
                        if (curr_line_start > 0) {
                            // 2. Find start of previous line
                            int prev_line_end = curr_line_start - 1;
                            int prev_line_start = prev_line_end;
                            while(prev_line_start > 0 && notepad_buffer[prev_line_start-1] != '\n') prev_line_start--;
                            
                            // 3. Move cursor to same column in previous line
                            int len_prev = prev_line_end - prev_line_start;
                            // Clamp column if prev line is shorter
                            if (col_curr > len_prev) col_curr = len_prev;
                            notepad_cursor = prev_line_start + col_curr;
                        }
                    }
                    else if (key_evt.character == KEY_DOWN) {
                        // Simple Line-Based Navigation (Reverted)
                        // 1. Find start of current line
                        int curr_line_start = notepad_cursor;
                        while(curr_line_start > 0 && notepad_buffer[curr_line_start-1] != '\n') curr_line_start--;
                        int col_curr = notepad_cursor - curr_line_start;
                        
                        // 2. Find end of current line
                        int curr_line_end = notepad_cursor;
                        while(notepad_buffer[curr_line_end] != 0 && notepad_buffer[curr_line_end] != '\n') curr_line_end++;
                        
                        // 3. Move to next line if exists
                        if (notepad_buffer[curr_line_end] == '\n') {
                            int next_line_start = curr_line_end + 1;
                            int next_line_end = next_line_start;
                            while(notepad_buffer[next_line_end] != 0 && notepad_buffer[next_line_end] != '\n') next_line_end++;
                            
                            int len_next = next_line_end - next_line_start;
                            if (col_curr > len_next) col_curr = len_next;
                            notepad_cursor = next_line_start + col_curr;
                        }
                    }
                    
                    screen_dirty = true;
                    min_dirty_index = 0; 
                }
                
                // Shortcuts
                else if ((key_evt.ctrl || key_evt.meta)) {
                     // Select All (A)
                     if (key_evt.character == 'a' || key_evt.character == 'A') {
                         selection_anchor = 0;
                         notepad_cursor = 0;
                         while(notepad_buffer[notepad_cursor] != 0) notepad_cursor++;
                         screen_dirty = true;
                         min_dirty_index = 0;
                     }
                     // Copy (C)
                     else if (key_evt.character == 'c' || key_evt.character == 'C') {
                         if (selection_anchor != -1) {
                             int start = (selection_anchor < notepad_cursor) ? selection_anchor : notepad_cursor;
                             int end = (selection_anchor < notepad_cursor) ? notepad_cursor : selection_anchor;
                             int len = end - start;
                             if (len > 4095) len = 4095;
                             for(int i=0; i<len; i++) system_clipboard[i] = notepad_buffer[start+i];
                             system_clipboard[len] = 0;
                         }
                     }
                     // Cut (X)
                     else if (key_evt.character == 'x' || key_evt.character == 'X') {
                         if (selection_anchor != -1) {
                             int start = (selection_anchor < notepad_cursor) ? selection_anchor : notepad_cursor;
                             int end = (selection_anchor < notepad_cursor) ? notepad_cursor : selection_anchor;
                             int len = end - start;
                             if (len > 4095) len = 4095;
                             for(int i=0; i<len; i++) system_clipboard[i] = notepad_buffer[start+i];
                             system_clipboard[len] = 0; // Null terminate
                             delete_selection();
                             selection_anchor = -1;
                             screen_dirty = true;
                             min_dirty_index = 0;
                         }
                     }
                     // Paste (V)
                     else if (key_evt.character == 'v' || key_evt.character == 'V') {
                         if (selection_anchor != -1) { 
                             delete_selection(); 
                             selection_anchor = -1; 
                         }
                         int i = 0;
                         while(system_clipboard[i] != 0) {
                             insert_char(system_clipboard[i]);
                             i++;
                         }
                         screen_dirty = true;
                         min_dirty_index = 0; // Paste changes a lot
                     }
                }
                
                // Typing
                else if (key_evt.character >= 32 || key_evt.character == '\n') {
                    if (selection_anchor != -1) {
                        delete_selection();
                        selection_anchor = -1;
                    }

                    if (min_dirty_index == -1 || notepad_cursor < min_dirty_index) 
                        min_dirty_index = notepad_cursor;

                    insert_char(key_evt.character);
                    screen_dirty = true;
                }
                
                // Backspace
                else if (key_evt.character == '\b') {
                    if (selection_anchor != -1) {
                        delete_selection();
                        selection_anchor = -1;
                        min_dirty_index = 0;
                    } else {
                        // Ctrl+Backspace: Delete Word Back
                        if (key_evt.ctrl || key_evt.meta) {
                             // Delete trailing spaces
                             while(notepad_cursor > 0 && notepad_buffer[notepad_cursor-1] == ' ') {
                                 delete_char_back();
                             }
                             // Delete word characters
                             while(notepad_cursor > 0 && notepad_buffer[notepad_cursor-1] != ' ') {
                                 delete_char_back();
                             }
                             min_dirty_index = notepad_cursor;
                        } else {
                            if (min_dirty_index == -1 || notepad_cursor < min_dirty_index)
                                min_dirty_index = notepad_cursor - 1;
                            delete_char_back();
                        }
                    }
                    screen_dirty = true;
                    if (min_dirty_index < 0) min_dirty_index = 0;
                }
            }
        }

        // Input: Mouse Click
        if (current.left_button && !last_mouse.left_button) { // Clicked just now
             int clicked = get_clicked_app(current.x, current.y);
             if (clicked != -1 && clicked != current_app) {
                 current_app = clicked;
                 screen_dirty = true;
                 min_dirty_index = 0; // Full redraw logic trigger 
             } else if (current_app == APP_NOTE) {
                 // Check for Scroll Bar Click (Right Edge)
                 // Assuming width 800, reserve 20px on right (780-800)
                 if (current.x >= 780) {
                      if (current.y < 300) {
                          // Scroll Up
                          if (notepad_scroll_y > 0) notepad_scroll_y--;
                      } else {
                          // Scroll Down
                          notepad_scroll_y++;
                      }
                      screen_dirty = true;
                      min_dirty_index = -1; // Full redraw needed for scroll
                 }
             }
        }
        
        // Input: Mouse Scroll Wheel (if supported)
        if (current.scroll_delta != 0) {
             if (current_app == APP_NOTE) {
                 // scroll_delta: negative = scroll wheel up (view earlier content), positive = scroll wheel down
                 int scroll_amount = current.scroll_delta * 3; // 3 lines per notch
                 notepad_scroll_y += scroll_amount;
                 if (notepad_scroll_y < 0) notepad_scroll_y = 0;
                 screen_dirty = true;
                 min_dirty_index = -1;
             }
             mouse_clear_scroll(); // Reset scroll delta after consuming
        }

        // Unified Redraw Logic
        bool mouse_moved = (current.x != last_mouse.x || current.y != last_mouse.y);
        
        if (screen_dirty || request_redraw || mouse_moved) {
             // If we scroll or heavily edit, force min_dirty to 0 to avoid artifacts
            if (min_dirty_index == -1 && screen_dirty) min_dirty_index = 0;
            
            // 1. Hide Cursor (restore background at OLD position)
            for(int y=0; y<16; y++) {
                for(int x=0; x<12; x++) {
                     framebuffer_put_pixel(last_mouse.x + x, last_mouse.y + y, cursor_backing_store[y*12 + x]);
                }
            }

            // 2. Update Content (if needed)
            if (screen_dirty || request_redraw) {
                draw_sidebar();
                draw_content(0);  // Always redraw content fully for stability
                request_redraw = false;
            }

            // 3. Update Mouse Position Logic
            if (mouse_moved) {
                last_mouse = current;
            }

            // 4. Save New Background at (potentially new) Mouse Position
            // optimization: only read from framebuffer, do not use get_pixel potentially if we had a backbuffer
            for(int y=0; y<16; y++) {
                for(int x=0; x<12; x++) {
                    cursor_backing_store[y*12 + x] = framebuffer_get_pixel(last_mouse.x + x, last_mouse.y + y);
                }
            }
            
            // 5. Draw Cursor at (potentially new) Position
            framebuffer_draw_cursor(last_mouse.x, last_mouse.y);
        }
        
        asm volatile("hlt");
    }
}
