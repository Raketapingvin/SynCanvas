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
bool request_redraw = true;

// --- NOTEPAD STATE ---
#define NOTE_BUF_SIZE 65536
char notepad_buffer[NOTE_BUF_SIZE];
int note_len = 0;           // total chars in buffer
int note_pos = 0;           // cursor position (insertion point)
int note_sel = -1;          // selection anchor (-1 = no selection)
int note_scroll_y = 0;      // scroll offset in lines
int note_total_lines = 1;   // cached total line count
int note_visible_lines = 1; // cached visible line count
bool note_sb_dragging = false; // scrollbar thumb dragging
int note_sb_drag_offset = 0;   // offset within thumb when drag started
char note_clipboard[NOTE_BUF_SIZE];
int note_clip_len = 0;

// Helper: get selection range (ordered)
static void note_get_sel(int* start, int* end) {
    if (note_sel < 0) { *start = *end = note_pos; return; }
    if (note_sel < note_pos) { *start = note_sel; *end = note_pos; }
    else { *start = note_pos; *end = note_sel; }
}

// Delete range [start, end) from buffer
static void note_delete_range(int start, int end) {
    if (start >= end) return;
    int rem = end - start;
    for (int i = end; i <= note_len; i++) notepad_buffer[i - rem] = notepad_buffer[i];
    note_len -= rem;
    note_pos = start;
    note_sel = -1;
}

// Insert string at note_pos
static void note_insert(const char* s, int slen) {
    if (note_len + slen >= NOTE_BUF_SIZE - 1) slen = NOTE_BUF_SIZE - 1 - note_len;
    if (slen <= 0) return;
    // Shift right
    for (int i = note_len; i >= note_pos; i--) notepad_buffer[i + slen] = notepad_buffer[i];
    for (int i = 0; i < slen; i++) notepad_buffer[note_pos + i] = s[i];
    note_len += slen;
    note_pos += slen;
    note_sel = -1;
}

// Is char a word boundary?
static int is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Move left by one word
static int word_left(int pos) {
    if (pos <= 0) return 0;
    pos--;
    while (pos > 0 && !is_word_char(notepad_buffer[pos])) pos--;
    while (pos > 0 && is_word_char(notepad_buffer[pos - 1])) pos--;
    return pos;
}

// Move right by one word
static int word_right(int pos) {
    if (pos >= note_len) return note_len;
    while (pos < note_len && !is_word_char(notepad_buffer[pos])) pos++;
    while (pos < note_len && is_word_char(notepad_buffer[pos])) pos++;
    return pos;
}

// Convert buffer position to line number
static int note_pos_to_line(int pos, int chars_per_line) {
    if (chars_per_line < 1) chars_per_line = 1;
    int line = 0;
    int col = 0;
    for (int i = 0; i < pos && i < note_len; i++) {
        if (notepad_buffer[i] == '\n') { line++; col = 0; }
        else { col++; if (col >= chars_per_line) { line++; col = 0; } }
    }
    return line;
}

// --- SETTINGS STATE ---
int setting_clock_24h = 1;      // 0 = 12h, 1 = 24h
int setting_theme = 0;          // 0 = Light, 1 = Dark, 2 = Blue, 3 = Green, 4 = Orange, 5 = Yellow
int setting_show_seconds = 0;   // 0 = off, 1 = on
int setting_show_date = 1;      // 0 = off, 1 = on
int settings_category = 0;      // 0=General, 1=Appearance, 2=Clock, 3=Network, 4=About
int setting_timezone = 0;       // offset in hours from UTC (-12 to +14), stored as index into tz table
int net_test_running = 0;       // 0=idle, 1=testing, 2=passed, 3=failed
int setting_mouse_speed = 1;    // 0=Slow, 1=Normal, 2=Fast
int setting_cursor_blink = 1;   // 0=off, 1=on
int setting_word_wrap = 1;      // 0=off, 1=on
int setting_line_numbers = 0;   // 0=off, 1=on
int setting_tab_size = 4;       // 2 or 4

// Timezone offsets (hours from UTC)
static const int tz_offsets[] = { -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
static const char* tz_labels[] = {
    "UTC-12", "UTC-11", "UTC-10", "UTC-9", "UTC-8", "UTC-7", "UTC-6", "UTC-5",
    "UTC-4", "UTC-3", "UTC-2", "UTC-1", "UTC+0", "UTC+1", "UTC+2", "UTC+3",
    "UTC+4", "UTC+5", "UTC+6", "UTC+7", "UTC+8", "UTC+9", "UTC+10", "UTC+11",
    "UTC+12", "UTC+13", "UTC+14"
};
#define TZ_COUNT 27
#define TZ_DEFAULT 12  // UTC+0

// Theme color sets: {bg, text, sidebar, select}
static uint32_t themes[][4] = {
    {0xFFFFFFFF, 0xFF000000, 0xFFDDDDDD, 0xFFAAAAAA}, // Light
    {0xFF1E1E1E, 0xFFE0E0E0, 0xFF2D2D2D, 0xFF444444}, // Dark 
    {0xFFE8F0FE, 0xFF1A237E, 0xFFBBDEFB, 0xFF90CAF9}, // Blue
    {0xFFE8F5E9, 0xFF1B5E20, 0xFFC8E6C9, 0xFFA5D6A7}, // Green
    {0xFFFFF3E0, 0xFFBF360C, 0xFFFFE0B2, 0xFFFFCC80}, // Orange
    {0xFFFFFDE7, 0xFFF57F17, 0xFFFFF9C4, 0xFFFFF176}, // Yellow
};
#define THEME_COUNT 6
static const char* theme_names[] = { "Light", "Dark", "Blue", "Green", "Orange", "Yellow" };

static uint32_t get_bg(void)      { return themes[setting_theme][0]; }
static uint32_t get_text(void)    { return themes[setting_theme][1]; }
static uint32_t get_sidebar(void) { return themes[setting_theme][2]; }
static uint32_t get_select(void)  { return themes[setting_theme][3]; }

// Cursor dimensions must match framebuffer_draw_cursor
#define CURSOR_W 12
#define CURSOR_H 16

// --- HELPERS ---

void print_2digits(int val, int x, int y, int scale) {
    char buf[3];
    buf[0] = '0' + (val / 10);
    buf[1] = '0' + (val % 10);
    buf[2] = 0;
    text_draw_string_scaled(buf, x, y, get_text(), get_bg(), scale);
}

void print_4digits(int val, int x, int y, int scale) {
    char buf[5];
    buf[0] = '0' + (val / 1000);
    buf[1] = '0' + ((val / 100) % 10);
    buf[2] = '0' + ((val / 10) % 10);
    buf[3] = '0' + (val % 10);
    buf[4] = 0;
    text_draw_string_scaled(buf, x, y, get_text(), get_bg(), scale);
}


// Check click hits
int get_clicked_app(int x, int y) {
    if (x < SIDEBAR_WIDTH) {
        if (y > 50 && y < 90) return APP_HOME;
        if (y > 100 && y < 140) return APP_NOTE;
        if (y > 150 && y < 190) return APP_SETTINGS;
    }
    return -1;
}

void draw_sidebar() {
    uint32_t sb_col = get_sidebar();
    uint32_t sel_col = get_select();
    uint32_t icon_col = get_text();

    // 1. Draw Sidebar Background
    framebuffer_draw_rect(0, 0, SIDEBAR_WIDTH, fb.height, sb_col); 
    
    // 2. Draw Divider Line
    framebuffer_draw_rect(SIDEBAR_WIDTH, 0, 2, fb.height, 0xFF999999);

    // 3. Draw Selection Highlight
    if (current_app == APP_HOME) {
        framebuffer_draw_rect(5, 50, 40, 40, sel_col);
    } else if (current_app == APP_NOTE) {
        framebuffer_draw_rect(5, 100, 40, 40, sel_col);
    } else if (current_app == APP_SETTINGS) {
        framebuffer_draw_rect(5, 150, 40, 40, sel_col);
    }

    // 4. Draw "House" Icon (at y=50) 
    // Roof
    for(int i=0; i<20; i++) {
         framebuffer_draw_rect(25-i, 50+10+i, i*2, 1, icon_col);
    }
    // Base
    framebuffer_draw_rect(15, 70+5, 20, 15, icon_col);
    // Door
    framebuffer_draw_rect(22, 75+5, 6, 10, sb_col); 

    // 5. Draw "Note" Icon (at y=100)
    framebuffer_draw_rect(15, 105, 20, 30, icon_col); // Paper shape
    framebuffer_draw_rect(17, 107, 16, 26, get_bg());  // White content
    // Lines
    framebuffer_draw_rect(19, 112, 12, 1, icon_col);
    framebuffer_draw_rect(19, 116, 12, 1, icon_col);
    framebuffer_draw_rect(19, 120, 12, 1, icon_col);

    // 6. Draw "Gear" Icon (at y=150)
    // Outer circle (approximate with rects)
    framebuffer_draw_rect(20, 155, 10, 2, icon_col);  // top
    framebuffer_draw_rect(20, 183, 10, 2, icon_col);  // bottom
    framebuffer_draw_rect(12, 163, 2, 10, icon_col);  // left
    framebuffer_draw_rect(36, 163, 2, 10, icon_col);  // right
    // Diagonal teeth
    framebuffer_draw_rect(14, 157, 3, 3, icon_col);   // top-left
    framebuffer_draw_rect(33, 157, 3, 3, icon_col);   // top-right
    framebuffer_draw_rect(14, 178, 3, 3, icon_col);   // bottom-left
    framebuffer_draw_rect(33, 178, 3, 3, icon_col);   // bottom-right
    // Center body
    framebuffer_draw_rect(17, 160, 16, 18, icon_col);
    framebuffer_draw_rect(15, 163, 20, 12, icon_col);
    // Center hole
    framebuffer_draw_rect(21, 165, 8, 8, sb_col);
}

// --- SETTINGS HELPERS ---
void draw_toggle(int x, int y, int on) {
    // Toggle track (40x20)
    uint32_t track_col = on ? COL_TOGGLE_ON : COL_TOGGLE_OFF;
    framebuffer_draw_rect(x, y, 40, 20, track_col);
    // Knob
    uint32_t knob_x = on ? x + 22 : x + 2;
    framebuffer_draw_rect(knob_x, y + 2, 16, 16, 0xFFFFFFFF);
}

void draw_radio(int x, int y, int selected) {
    // Simple radio: filled square if selected, outline if not
    framebuffer_draw_rect(x, y, 14, 14, get_text());
    framebuffer_draw_rect(x + 2, y + 2, 10, 10, get_bg());
    if (selected) {
        framebuffer_draw_rect(x + 4, y + 4, 6, 6, COL_ACCENT);
    }
}

#define SETTINGS_CAT_GENERAL 0
#define SETTINGS_CAT_APPEARANCE 1
#define SETTINGS_CAT_CLOCK 2
#define SETTINGS_CAT_NETWORK 3
#define SETTINGS_CAT_ABOUT 4
#define SETTINGS_TAB_WIDTH 80
#define SETTINGS_TAB_HEIGHT 28

void draw_settings_tab(int x, int y, const char* label, int selected) {
    uint32_t bg = selected ? get_bg() : get_select();
    uint32_t fg = selected ? COL_ACCENT : get_text();
    framebuffer_draw_rect(x, y, SETTINGS_TAB_WIDTH, SETTINGS_TAB_HEIGHT, bg);
    // Bottom highlight for selected tab
    if (selected) {
        framebuffer_draw_rect(x, y + SETTINGS_TAB_HEIGHT - 3, SETTINGS_TAB_WIDTH, 3, COL_ACCENT);
    }
    text_draw_string(label, x + 10, y + 9, fg, bg);
}

void draw_settings_page() {
    uint32_t bg = get_bg();
    uint32_t fg = get_text();
    int cx = SIDEBAR_WIDTH + 20;
    int cy = 20;

    text_draw_string_scaled("Settings", cx, cy, fg, bg, 3);
    cy += 38;

    // --- Category Tabs ---
    int tab_x = cx;
    draw_settings_tab(tab_x, cy, "General", settings_category == SETTINGS_CAT_GENERAL);
    tab_x += SETTINGS_TAB_WIDTH + 4;
    draw_settings_tab(tab_x, cy, "Appearance", settings_category == SETTINGS_CAT_APPEARANCE);
    tab_x += SETTINGS_TAB_WIDTH + 4;
    draw_settings_tab(tab_x, cy, "Clock", settings_category == SETTINGS_CAT_CLOCK);
    tab_x += SETTINGS_TAB_WIDTH + 4;
    draw_settings_tab(tab_x, cy, "Network", settings_category == SETTINGS_CAT_NETWORK);
    tab_x += SETTINGS_TAB_WIDTH + 4;
    draw_settings_tab(tab_x, cy, "About", settings_category == SETTINGS_CAT_ABOUT);
    cy += SETTINGS_TAB_HEIGHT;

    // Tab bar underline
    framebuffer_draw_rect(cx, cy, fb.width - SIDEBAR_WIDTH - 40, 1, 0xFF999999);
    cy += 15;

    // --- Category Content ---
    if (settings_category == SETTINGS_CAT_GENERAL) {
        text_draw_string_scaled("Mouse", cx, cy, COL_ACCENT, bg, 2);
        cy += 28;

        text_draw_string("Mouse Speed", cx, cy + 3, fg, bg);
        // Speed selector: 3 radios
        static const char* speed_names[] = { "Slow", "Normal", "Fast" };
        for (int i = 0; i < 3; i++) {
            int rx = cx + 150 + i * 75;
            draw_radio(rx, cy + 2, setting_mouse_speed == i);
            text_draw_string(speed_names[i], rx + 18, cy + 3, fg, bg);
        }
        cy += 32;

        framebuffer_draw_rect(cx, cy, 300, 1, 0xFF999999);
        cy += 15;

        text_draw_string_scaled("Editor", cx, cy, COL_ACCENT, bg, 2);
        cy += 28;

        text_draw_string("Cursor blink", cx, cy + 3, fg, bg);
        draw_toggle(cx + 200, cy, setting_cursor_blink);
        cy += 32;

        text_draw_string("Word wrap", cx, cy + 3, fg, bg);
        draw_toggle(cx + 200, cy, setting_word_wrap);
        cy += 32;

        text_draw_string("Line numbers", cx, cy + 3, fg, bg);
        draw_toggle(cx + 200, cy, setting_line_numbers);
        cy += 32;

        text_draw_string("Tab size", cx, cy + 3, fg, bg);
        // Tab size selector: 2-space or 4-space
        int rx2 = cx + 150;
        draw_radio(rx2, cy + 2, setting_tab_size == 2);
        text_draw_string("2", rx2 + 18, cy + 3, fg, bg);
        rx2 += 50;
        draw_radio(rx2, cy + 2, setting_tab_size == 4);
        text_draw_string("4", rx2 + 18, cy + 3, fg, bg);
        cy += 32;

    } else if (settings_category == SETTINGS_CAT_APPEARANCE) {
        text_draw_string_scaled("Theme", cx, cy, COL_ACCENT, bg, 2);
        cy += 28;

        // Draw theme radios in 2 rows of 3
        for (int i = 0; i < THEME_COUNT; i++) {
            int col = i % 3;
            int row = i / 3;
            int rx = cx + col * 90;
            int ry = cy + row * 24;
            draw_radio(rx, ry + 2, setting_theme == i);
            text_draw_string(theme_names[i], rx + 20, ry + 3, fg, bg);
        }
        cy += 24 * 2 + 10;

        // Theme preview box
        text_draw_string("Preview:", cx, cy, 0xFF888888, bg);
        cy += 14;
        uint32_t prev_bg = themes[setting_theme][0];
        uint32_t prev_fg = themes[setting_theme][1];
        uint32_t prev_sb = themes[setting_theme][2];
        framebuffer_draw_rect(cx, cy, 200, 50, prev_sb);
        framebuffer_draw_rect(cx + 30, cy, 170, 50, prev_bg);
        text_draw_string("Abc", cx + 50, cy + 20, prev_fg, prev_bg);
        framebuffer_draw_rect(cx, cy, 200, 1, 0xFF999999);
        framebuffer_draw_rect(cx, cy + 49, 200, 1, 0xFF999999);
        framebuffer_draw_rect(cx, cy, 1, 50, 0xFF999999);
        framebuffer_draw_rect(cx + 199, cy, 1, 50, 0xFF999999);

    } else if (settings_category == SETTINGS_CAT_CLOCK) {
        text_draw_string_scaled("Time Format", cx, cy, COL_ACCENT, bg, 2);
        cy += 28;

        text_draw_string("24-hour format", cx, cy + 3, fg, bg);
        draw_toggle(cx + 200, cy, setting_clock_24h);
        cy += 32;

        text_draw_string("Show seconds", cx, cy + 3, fg, bg);
        draw_toggle(cx + 200, cy, setting_show_seconds);
        cy += 40;

        // Divider
        framebuffer_draw_rect(cx, cy, 300, 1, 0xFF999999);
        cy += 15;

        text_draw_string_scaled("Date", cx, cy, COL_ACCENT, bg, 2);
        cy += 28;

        text_draw_string("Show date on home", cx, cy + 3, fg, bg);
        draw_toggle(cx + 200, cy, setting_show_date);
        cy += 40;

        // Divider
        framebuffer_draw_rect(cx, cy, 300, 1, 0xFF999999);
        cy += 15;

        text_draw_string_scaled("Time Zone", cx, cy, COL_ACCENT, bg, 2);
        cy += 28;

        // Timezone selector: show current with < > arrows
        // Left arrow
        framebuffer_draw_rect(cx, cy, 20, 20, get_select());
        text_draw_string("<", cx + 6, cy + 5, fg, get_select());
        // Current timezone label
        framebuffer_draw_rect(cx + 24, cy, 80, 20, get_select());
        text_draw_string(tz_labels[setting_timezone], cx + 30, cy + 5, fg, get_select());
        // Right arrow
        framebuffer_draw_rect(cx + 108, cy, 20, 20, get_select());
        text_draw_string(">", cx + 114, cy + 5, fg, get_select());
        cy += 30;

    } else if (settings_category == SETTINGS_CAT_NETWORK) {
        text_draw_string_scaled("Network", cx, cy, COL_ACCENT, bg, 2);
        cy += 28;

        // Network adapter status
        int net_found = rtl8139_is_detected();
        text_draw_string("Adapter:", cx, cy, fg, bg);
        if (net_found) {
            text_draw_string("RTL8139 (Detected)", cx + 80, cy, COL_TOGGLE_ON, bg);
        } else {
            text_draw_string("Not detected", cx + 80, cy, 0xFFCC0000, bg);
        }
        cy += 16;

        // MAC address
        if (net_found) {
            uint8_t mac[6];
            rtl8139_get_mac(mac);
            char mac_str[18];
            for (int i = 0; i < 6; i++) {
                mac_str[i*3]   = "0123456789ABCDEF"[mac[i] >> 4];
                mac_str[i*3+1] = "0123456789ABCDEF"[mac[i] & 0xF];
                mac_str[i*3+2] = (i < 5) ? ':' : '\0';
            }
            mac_str[17] = '\0';
            text_draw_string("MAC:", cx, cy, fg, bg);
            text_draw_string(mac_str, cx + 80, cy, fg, bg);
            cy += 16;

            text_draw_string("Status:", cx, cy, fg, bg);
            text_draw_string("Link Up (Emulated)", cx + 80, cy, COL_TOGGLE_ON, bg);
        } else {
            text_draw_string("Status:", cx, cy, fg, bg);
            text_draw_string("No link", cx + 80, cy, 0xFFCC0000, bg);
        }
        cy += 24;

        // Divider
        framebuffer_draw_rect(cx, cy, 300, 1, 0xFF999999);
        cy += 15;

        // Network test button
        text_draw_string_scaled("Network Test", cx, cy, COL_ACCENT, bg, 2);
        cy += 28;

        text_draw_string("Sends ARP request to 10.0.2.2 (gateway)", cx, cy, 0xFF888888, bg);
        cy += 16;

        // "Run Test" button
        uint32_t btn_col = COL_ACCENT;
        framebuffer_draw_rect(cx, cy, 140, 26, btn_col);
        text_draw_string("ARP Ping Test", cx + 12, cy + 8, 0xFFFFFFFF, btn_col);

        // Test result
        cy += 30;
        if (net_test_running == 1) {
            text_draw_string("Sending ARP request...", cx, cy, 0xFFAAAA00, bg);
        } else if (net_test_running == 2) {
            text_draw_string("PASS - ARP reply received from gateway!", cx, cy, COL_TOGGLE_ON, bg);
            cy += 14;
            text_draw_string("Network connectivity confirmed.", cx, cy, COL_TOGGLE_ON, bg);
        } else if (net_test_running == 3) {
            text_draw_string("FAIL - No ARP reply (timeout 2s)", cx, cy, 0xFFCC0000, bg);
            cy += 14;
            text_draw_string("Check network adapter & link.", cx, cy, 0xFFCC0000, bg);
        } else if (net_test_running == 4) {
            text_draw_string("FAIL - Could not transmit packet", cx, cy, 0xFFCC0000, bg);
        }
        cy += 36;

    } else if (settings_category == SETTINGS_CAT_ABOUT) {
        text_draw_string_scaled("SynCanvas", cx, cy, COL_ACCENT, bg, 2);
        cy += 28;

        text_draw_string("The innovation in operating systems,", cx, cy, fg, bg); cy += 12;
        text_draw_string("no windows, just one canvas, where", cx, cy, fg, bg); cy += 12;
        text_draw_string("everything is possible!", cx, cy, fg, bg); cy += 16;
        text_draw_string("By: Raketapingvin", cx, cy, COL_ACCENT, bg); cy += 20;

        framebuffer_draw_rect(cx, cy, 300, 1, 0xFF999999);
        cy += 12;

        text_draw_string("Version: 1.2.0 - (builds on github)", cx, cy, fg, bg); cy += 20;

        framebuffer_draw_rect(cx, cy, 300, 1, 0xFF999999);
        cy += 12;

        text_draw_string_scaled("System Info", cx, cy, COL_ACCENT, bg, 2);
        cy += 24;

        text_draw_string("Platform:    x86_64", cx, cy, fg, bg); cy += 12;
        text_draw_string("Kernel:      Monolithic", cx, cy, fg, bg); cy += 12;

        // Display resolution
        char res_buf[40];
        // Build resolution string manually
        int ri = 0;
        { // width
            int w = fb.width;
            char tmp[6]; int ti = 0;
            if (w == 0) { tmp[ti++] = '0'; }
            else { while(w > 0) { tmp[ti++] = '0' + (w % 10); w /= 10; } }
            for (int j = ti - 1; j >= 0; j--) res_buf[ri++] = tmp[j];
        }
        res_buf[ri++] = 'x';
        { // height
            int h = fb.height;
            char tmp[6]; int ti = 0;
            if (h == 0) { tmp[ti++] = '0'; }
            else { while(h > 0) { tmp[ti++] = '0' + (h % 10); h /= 10; } }
            for (int j = ti - 1; j >= 0; j--) res_buf[ri++] = tmp[j];
        }
        res_buf[ri++] = 'x';
        { // bpp
            int b = fb.bpp;
            char tmp[4]; int ti = 0;
            if (b == 0) { tmp[ti++] = '0'; }
            else { while(b > 0) { tmp[ti++] = '0' + (b % 10); b /= 10; } }
            for (int j = ti - 1; j >= 0; j--) res_buf[ri++] = tmp[j];
        }
        res_buf[ri] = '\0';
        text_draw_string("Display:     ", cx, cy, fg, bg);
        text_draw_string(res_buf, cx + 104, cy, fg, bg); cy += 12;

        text_draw_string("Timer:       PIT @ 100Hz", cx, cy, fg, bg); cy += 12;
        text_draw_string("RTC:         CMOS Real-Time Clock", cx, cy, fg, bg); cy += 16;

        framebuffer_draw_rect(cx, cy, 300, 1, 0xFF999999);
        cy += 12;

        text_draw_string("Drivers:", cx, cy, fg, bg); cy += 14;
        text_draw_string("  PS/2 Keyboard & Mouse", cx, cy, 0xFF888888, bg); cy += 12;
        text_draw_string("  VGA / Framebuffer display", cx, cy, 0xFF888888, bg); cy += 12;
        text_draw_string("  ATA / AHCI / NVMe storage", cx, cy, 0xFF888888, bg); cy += 12;
        text_draw_string("  RTL8139 network", cx, cy, 0xFF888888, bg); cy += 12;
        text_draw_string("  USB (UHCI/xHCI)", cx, cy, 0xFF888888, bg); cy += 12;
        text_draw_string("  AC97 / HDA / PC Speaker audio", cx, cy, 0xFF888888, bg); cy += 12;
    }
}

// Check if a settings control was clicked, return 1 if changed
int handle_settings_click(int mx, int my) {
    int cx = SIDEBAR_WIDTH + 20;
    int cy = 20 + 38; // after title, at tab row

    // --- Tab clicks ---
    if (my >= cy && my < cy + SETTINGS_TAB_HEIGHT) {
        int tab_x = cx;
        if (mx >= tab_x && mx < tab_x + SETTINGS_TAB_WIDTH) {
            settings_category = SETTINGS_CAT_GENERAL; return 1;
        }
        tab_x += SETTINGS_TAB_WIDTH + 4;
        if (mx >= tab_x && mx < tab_x + SETTINGS_TAB_WIDTH) {
            settings_category = SETTINGS_CAT_APPEARANCE; return 1;
        }
        tab_x += SETTINGS_TAB_WIDTH + 4;
        if (mx >= tab_x && mx < tab_x + SETTINGS_TAB_WIDTH) {
            settings_category = SETTINGS_CAT_CLOCK; return 1;
        }
        tab_x += SETTINGS_TAB_WIDTH + 4;
        if (mx >= tab_x && mx < tab_x + SETTINGS_TAB_WIDTH) {
            settings_category = SETTINGS_CAT_NETWORK; return 1;
        }
        tab_x += SETTINGS_TAB_WIDTH + 4;
        if (mx >= tab_x && mx < tab_x + SETTINGS_TAB_WIDTH) {
            settings_category = SETTINGS_CAT_ABOUT; return 1;
        }
        return 0;
    }

    cy += SETTINGS_TAB_HEIGHT + 1 + 15; // past tabs + underline + padding

    // --- Category content clicks ---
    if (settings_category == SETTINGS_CAT_GENERAL) {
        cy += 28; // past "Mouse" heading

        // Mouse speed radios
        for (int i = 0; i < 3; i++) {
            int rx = cx + 150 + i * 75;
            if (my >= cy && my <= cy + 18 && mx >= rx && mx < rx + 60) {
                setting_mouse_speed = i;
                mouse_speed_setting = i;
                return 1;
            }
        }
        cy += 32;
        cy += 15 + 1; // divider
        cy += 28; // past "Editor" heading

        // Cursor blink toggle
        if (my >= cy && my <= cy + 20 && mx >= cx + 200 && mx <= cx + 240) {
            setting_cursor_blink = !setting_cursor_blink; return 1;
        }
        cy += 32;

        // Word wrap toggle
        if (my >= cy && my <= cy + 20 && mx >= cx + 200 && mx <= cx + 240) {
            setting_word_wrap = !setting_word_wrap; return 1;
        }
        cy += 32;

        // Line numbers toggle
        if (my >= cy && my <= cy + 20 && mx >= cx + 200 && mx <= cx + 240) {
            setting_line_numbers = !setting_line_numbers; return 1;
        }
        cy += 32;

        // Tab size radios
        int rx2 = cx + 150;
        if (my >= cy && my <= cy + 18 && mx >= rx2 && mx < rx2 + 40) {
            setting_tab_size = 2; return 1;
        }
        rx2 += 50;
        if (my >= cy && my <= cy + 18 && mx >= rx2 && mx < rx2 + 40) {
            setting_tab_size = 4; return 1;
        }
    } else if (settings_category == SETTINGS_CAT_APPEARANCE) {
        cy += 28; // past "Theme" heading

        // Theme radios: 2 rows of 3
        for (int i = 0; i < THEME_COUNT; i++) {
            int col = i % 3;
            int row = i / 3;
            int rx = cx + col * 90;
            int ry = cy + row * 24;
            if (my >= ry && my <= ry + 18 && mx >= rx && mx < rx + 80) {
                setting_theme = i; return 1;
            }
        }
    } else if (settings_category == SETTINGS_CAT_CLOCK) {
        cy += 28; // past "Time Format" heading

        // 24h toggle
        if (my >= cy && my <= cy + 20 && mx >= cx + 200 && mx <= cx + 240) {
            setting_clock_24h = !setting_clock_24h; return 1;
        }
        cy += 32;

        // Show seconds toggle
        if (my >= cy && my <= cy + 20 && mx >= cx + 200 && mx <= cx + 240) {
            setting_show_seconds = !setting_show_seconds; return 1;
        }
        cy += 40 + 15 + 1; // past divider
        cy += 28; // past "Date" heading

        // Show date toggle
        if (my >= cy && my <= cy + 20 && mx >= cx + 200 && mx <= cx + 240) {
            setting_show_date = !setting_show_date; return 1;
        }
        cy += 40 + 15 + 1; // past divider
        cy += 28; // past "Time Zone" heading

        // Timezone left arrow
        if (my >= cy && my <= cy + 20 && mx >= cx && mx < cx + 20) {
            if (setting_timezone > 0) setting_timezone--;
            return 1;
        }
        // Timezone right arrow
        if (my >= cy && my <= cy + 20 && mx >= cx + 108 && mx < cx + 128) {
            if (setting_timezone < TZ_COUNT - 1) setting_timezone++;
            return 1;
        }
    } else if (settings_category == SETTINGS_CAT_NETWORK) {
        cy += 28; // past "Network" heading
        // Skip adapter info lines
        int net_found = rtl8139_is_detected();
        cy += 16; // Adapter line
        if (net_found) {
            cy += 16; // MAC line
        }
        cy += 24; // Status line + gap
        cy += 15 + 1; // divider
        cy += 28; // past "Network Test" heading
        cy += 16; // past description text

        // "ARP Ping Test" button at (cx, cy) size 140x26
        if (my >= cy && my <= cy + 26 && mx >= cx && mx <= cx + 140) {
            net_test_running = 1; // show "testing" state
            // Actually send an ARP ping to QEMU gateway 10.0.2.2
            int result = rtl8139_arp_ping(10, 0, 2, 2);
            if (result == 1) {
                net_test_running = 2; // pass - got ARP reply
            } else {
                net_test_running = 3; // fail - no reply
            }
            return 1;
        }
    }

    return 0;
}

void draw_content() {
    uint32_t bg = get_bg();
    uint32_t fg = get_text();

    // Clear Content Area
    framebuffer_draw_rect(SIDEBAR_WIDTH + 2, 0, fb.width - SIDEBAR_WIDTH - 2, fb.height, bg);

    if (current_app == APP_HOME) {
        text_draw_string_scaled("Welcome to SynCanvas", SIDEBAR_WIDTH + 20, 50, fg, bg, 2);
        
        Time t = rtc_get_time();
        int base_y = 90;
        int scale = 3;
        int char_w = 8 * scale;
        
        // Apply timezone offset
        int tz_off = tz_offsets[setting_timezone];
        int adj_hours = (int)t.hours + tz_off;
        if (adj_hours < 0) adj_hours += 24;
        if (adj_hours >= 24) adj_hours -= 24;

        // Format hours based on clock_24h setting
        int display_hours = adj_hours;
        int is_pm = 0;
        if (!setting_clock_24h) {
            is_pm = display_hours >= 12;
            display_hours = display_hours % 12;
            if (display_hours == 0) display_hours = 12;
        }

        text_draw_string_scaled("Time: ", SIDEBAR_WIDTH + 20, base_y, fg, bg, scale);
        int x_off = SIDEBAR_WIDTH + 20 + (6 * char_w);
        print_2digits(display_hours, x_off, base_y, scale);
        
        x_off += (2 * char_w);
        text_draw_string_scaled(":", x_off, base_y, fg, bg, scale);
        
        x_off += char_w;
        print_2digits(t.minutes, x_off, base_y, scale);

        if (setting_show_seconds) {
            x_off += (2 * char_w);
            text_draw_string_scaled(":", x_off, base_y, fg, bg, scale);
            x_off += char_w;
            print_2digits(t.seconds, x_off, base_y, scale);
        }

        if (!setting_clock_24h) {
            x_off += (2 * char_w) + char_w / 2;
            text_draw_string_scaled(is_pm ? "PM" : "AM", x_off, base_y, fg, bg, scale);
        }

        if (setting_show_date) {
            base_y += (10 * scale); // New line
            text_draw_string_scaled("Date: ", SIDEBAR_WIDTH + 20, base_y, fg, bg, scale);
            
            x_off = SIDEBAR_WIDTH + 20 + (6 * char_w);
            print_2digits(t.day, x_off, base_y, scale);
            
            x_off += (2 * char_w);
            text_draw_string_scaled("/", x_off, base_y, fg, bg, scale);
            
            x_off += char_w;
            print_2digits(t.month, x_off, base_y, scale);
            
            x_off += (2 * char_w);
            text_draw_string_scaled("/", x_off, base_y, fg, bg, scale);
            
            x_off += char_w;
            print_4digits(2000 + t.year, x_off, base_y, scale);
        }

    } else if (current_app == APP_SETTINGS) {
        draw_settings_page();

    } else if (current_app == APP_NOTE) {
        int line_num_w = setting_line_numbers ? 36 : 0; // width for line number gutter
        int text_area_x = SIDEBAR_WIDTH + 20 + line_num_w;
        int text_area_y = 60;
        int scrollbar_w = 12;
        int text_area_w = fb.width - text_area_x - 20 - scrollbar_w;
        int text_area_h = fb.height - text_area_y - 10;
        int line_h = 12;
        int char_w = 8;
        int max_chars_per_line = setting_word_wrap ? (text_area_w / char_w) : 10000;
        if (max_chars_per_line < 1) max_chars_per_line = 1;
        int visible_lines = text_area_h / line_h;

        text_draw_string("Notepad", SIDEBAR_WIDTH + 20, 20, fg, bg);
        // Show char count
        {
            char count_buf[32] = "Chars: ";
            int ci = 7;
            int n = note_len;
            if (n == 0) { count_buf[ci++] = '0'; }
            else {
                char tmp[8]; int ti = 0;
                while (n > 0) { tmp[ti++] = '0' + (n % 10); n /= 10; }
                for (int j = ti - 1; j >= 0; j--) count_buf[ci++] = tmp[j];
            }
            count_buf[ci] = '\0';
            text_draw_string(count_buf, SIDEBAR_WIDTH + 20, 40, 0xFFAAAAAA, bg);
        }
        text_draw_string("Ctrl+A/C/X/V | Arrows | Ctrl+Arrows", SIDEBAR_WIDTH + 200, 40, 0xFFAAAAAA, bg);

        // Count total lines
        int total_lines = 1;
        int col = 0;
        for (int i = 0; i < note_len; i++) {
            if (notepad_buffer[i] == '\n') { total_lines++; col = 0; }
            else { col++; if (col >= max_chars_per_line) { total_lines++; col = 0; } }
        }
        // Cache for scroll/drag calculations
        note_total_lines = total_lines;
        note_visible_lines = visible_lines;

        // Ensure cursor line is visible (auto-scroll)
        int cursor_line = note_pos_to_line(note_pos, max_chars_per_line);
        if (cursor_line < note_scroll_y) note_scroll_y = cursor_line;
        if (cursor_line >= note_scroll_y + visible_lines) note_scroll_y = cursor_line - visible_lines + 1;
        if (note_scroll_y < 0) note_scroll_y = 0;

        // Get selection range
        int sel_start, sel_end;
        note_get_sel(&sel_start, &sel_end);

        // Cursor blink: visible for 500ms, hidden for 500ms
        uint64_t blink_tick = get_tick_count();
        int cursor_visible = 1;
        if (setting_cursor_blink) {
            cursor_visible = ((blink_tick / 50) % 2 == 0) ? 1 : 0; // 500ms on/off at 100Hz
        }

        // Line number gutter background
        if (setting_line_numbers) {
            framebuffer_draw_rect(SIDEBAR_WIDTH + 18, text_area_y, line_num_w, text_area_h, get_select());
        }

        // Draw text with selection highlighting and line numbers
        int draw_line = 0;
        int draw_col = 0;
        int last_drawn_line = -1;
        for (int i = 0; i <= note_len; i++) {
            int screen_line = draw_line - note_scroll_y;
            if (screen_line >= visible_lines) break;

            // Draw line number
            if (setting_line_numbers && screen_line >= 0 && draw_line != last_drawn_line) {
                last_drawn_line = draw_line;
                int ln = draw_line + 1;
                char lnbuf[6];
                int li = 0;
                if (ln >= 1000) lnbuf[li++] = '0' + (ln / 1000) % 10;
                if (ln >= 100)  lnbuf[li++] = '0' + (ln / 100) % 10;
                if (ln >= 10)   lnbuf[li++] = '0' + (ln / 10) % 10;
                lnbuf[li++] = '0' + ln % 10;
                lnbuf[li] = '\0';
                int lx = SIDEBAR_WIDTH + 20 + (line_num_w - 4 - li * 8);
                text_draw_string(lnbuf, lx, text_area_y + screen_line * line_h + 2, 0xFF888888, get_select());
            }

            // Draw cursor at this position
            if (i == note_pos && screen_line >= 0 && cursor_visible) {
                int cx = text_area_x + draw_col * char_w;
                int cy2 = text_area_y + screen_line * line_h;
                framebuffer_draw_rect(cx, cy2, 2, line_h - 2, fg);
            }

            if (i >= note_len) break;

            if (screen_line >= 0) {
                int cx = text_area_x + draw_col * char_w;
                int cy2 = text_area_y + screen_line * line_h;

                // Selection background
                if (note_sel >= 0 && i >= sel_start && i < sel_end) {
                    framebuffer_draw_rect(cx, cy2, char_w, line_h, COL_ACCENT);
                    if (notepad_buffer[i] != '\n')
                        text_draw_char(notepad_buffer[i], cx, cy2 + 2, 0xFFFFFFFF, COL_ACCENT);
                } else {
                    if (notepad_buffer[i] != '\n')
                        text_draw_char(notepad_buffer[i], cx, cy2 + 2, fg, bg);
                }
            }

            if (notepad_buffer[i] == '\n') {
                draw_line++; draw_col = 0;
            } else {
                draw_col++;
                if (draw_col >= max_chars_per_line) { draw_line++; draw_col = 0; }
            }
        }

        // --- Scrollbar ---
        if (total_lines > visible_lines) {
            int sb_x = fb.width - 20 - scrollbar_w;
            int sb_y = text_area_y;
            int sb_h = text_area_h;

            // Track
            framebuffer_draw_rect(sb_x, sb_y, scrollbar_w, sb_h, get_select());

            // Thumb
            int thumb_h = (visible_lines * sb_h) / total_lines;
            if (thumb_h < 16) thumb_h = 16;
            int thumb_y = sb_y + (note_scroll_y * (sb_h - thumb_h)) / (total_lines - visible_lines);
            framebuffer_draw_rect(sb_x + 2, thumb_y, scrollbar_w - 4, thumb_h, COL_ACCENT);
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
    draw_content();

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
    uint64_t last_clock_tick = get_tick_count();

    while (1) {
        // Mouse is now interrupt-driven (IRQ 12), no polling needed

        struct MouseState current = mouse_get_state();
        KeyEvent kevt = keyboard_get_event();

        bool screen_dirty = false;

        // Periodic clock update: redraw every 1 second (100 ticks at 100Hz)
        uint64_t now_tick = get_tick_count();
        if (current_app == APP_HOME && (now_tick - last_clock_tick) >= 100) {
            last_clock_tick = now_tick;
            screen_dirty = true;
        }

        // Cursor blink: redraw notepad every 500ms when blink is enabled
        if (current_app == APP_NOTE && setting_cursor_blink && (now_tick - last_clock_tick) >= 50) {
            last_clock_tick = now_tick;
            screen_dirty = true;
        }

        // Input: Keyboard (Notepad)
        if (kevt.character != 0 && current_app == APP_NOTE) {
            char c = kevt.character;
            bool ctrl = kevt.ctrl;
            bool shift = kevt.shift;

            if (ctrl && (c == 'a' || c == 'A')) {
                // Ctrl+A: Select All
                note_sel = 0;
                note_pos = note_len;
                screen_dirty = true;
            } else if (ctrl && (c == 'c' || c == 'C')) {
                // Ctrl+C: Copy
                int s, e;
                note_get_sel(&s, &e);
                if (s < e) {
                    note_clip_len = e - s;
                    for (int i = 0; i < note_clip_len; i++) note_clipboard[i] = notepad_buffer[s + i];
                }
            } else if (ctrl && (c == 'x' || c == 'X')) {
                // Ctrl+X: Cut
                int s, e;
                note_get_sel(&s, &e);
                if (s < e) {
                    note_clip_len = e - s;
                    for (int i = 0; i < note_clip_len; i++) note_clipboard[i] = notepad_buffer[s + i];
                    note_delete_range(s, e);
                    screen_dirty = true;
                }
            } else if (ctrl && (c == 'v' || c == 'V')) {
                // Ctrl+V: Paste
                if (note_clip_len > 0) {
                    // Delete selection first if any
                    if (note_sel >= 0) {
                        int s, e;
                        note_get_sel(&s, &e);
                        note_delete_range(s, e);
                    }
                    note_insert(note_clipboard, note_clip_len);
                    screen_dirty = true;
                }
            } else if (c == KEY_LEFT) {
                if (ctrl && shift) {
                    // Ctrl+Shift+Left: extend selection word left
                    if (note_sel < 0) note_sel = note_pos;
                    note_pos = word_left(note_pos);
                } else if (ctrl) {
                    // Ctrl+Left: word left
                    note_pos = word_left(note_pos);
                    note_sel = -1;
                } else if (shift) {
                    // Shift+Left: extend selection
                    if (note_sel < 0) note_sel = note_pos;
                    if (note_pos > 0) note_pos--;
                } else {
                    // Left: move cursor
                    if (note_sel >= 0) {
                        int s, e;
                        note_get_sel(&s, &e);
                        note_pos = s;
                        note_sel = -1;
                    } else if (note_pos > 0) note_pos--;
                }
                screen_dirty = true;
            } else if (c == KEY_RIGHT) {
                if (ctrl && shift) {
                    if (note_sel < 0) note_sel = note_pos;
                    note_pos = word_right(note_pos);
                } else if (ctrl) {
                    note_pos = word_right(note_pos);
                    note_sel = -1;
                } else if (shift) {
                    if (note_sel < 0) note_sel = note_pos;
                    if (note_pos < note_len) note_pos++;
                } else {
                    if (note_sel >= 0) {
                        int s, e;
                        note_get_sel(&s, &e);
                        note_pos = e;
                        note_sel = -1;
                    } else if (note_pos < note_len) note_pos++;
                }
                screen_dirty = true;
            } else if (c == KEY_UP) {
                // Move up one visual line
                int cpl = (fb.width - SIDEBAR_WIDTH - 20 - 20 - 12) / 8;
                if (cpl < 1) cpl = 1;
                // Find current column
                int line_start = note_pos;
                int cur_col = 0;
                // Walk backwards to find start of visual line
                int tmp_col = 0;
                int tmp_line_start = 0;
                for (int i = 0; i < note_pos; i++) {
                    if (notepad_buffer[i] == '\n') { tmp_col = 0; tmp_line_start = i + 1; }
                    else { tmp_col++; if (tmp_col >= cpl) { tmp_col = 0; tmp_line_start = i + 1; } }
                }
                cur_col = note_pos - tmp_line_start;
                // Move to previous line, same column
                if (tmp_line_start > 0) {
                    // Find previous line start
                    int prev_end = tmp_line_start - 1;
                    int prev_start = 0;
                    int pc = 0;
                    for (int i = 0; i < prev_end; i++) {
                        if (notepad_buffer[i] == '\n') { pc = 0; prev_start = i + 1; }
                        else { pc++; if (pc >= cpl) { pc = 0; prev_start = i + 1; } }
                    }
                    int prev_len = prev_end - prev_start;
                    if (notepad_buffer[prev_end] != '\n') prev_len++;
                    int target_col = cur_col;
                    if (target_col > prev_len) target_col = prev_len;
                    if (shift) { if (note_sel < 0) note_sel = note_pos; }
                    else note_sel = -1;
                    note_pos = prev_start + target_col;
                }
                screen_dirty = true;
            } else if (c == KEY_DOWN) {
                int cpl = (fb.width - SIDEBAR_WIDTH - 20 - 20 - 12) / 8;
                if (cpl < 1) cpl = 1;
                // Find current visual line start and column
                int tmp_col = 0;
                int tmp_line_start = 0;
                for (int i = 0; i < note_pos; i++) {
                    if (notepad_buffer[i] == '\n') { tmp_col = 0; tmp_line_start = i + 1; }
                    else { tmp_col++; if (tmp_col >= cpl) { tmp_col = 0; tmp_line_start = i + 1; } }
                }
                int cur_col = note_pos - tmp_line_start;
                // Find end of current visual line
                int next_start = note_pos;
                int nc = tmp_col + (note_pos - tmp_line_start - tmp_col); // already at cur_col
                // Walk to end of this line
                int wc = cur_col;
                for (int i = note_pos; i < note_len; i++) {
                    if (notepad_buffer[i] == '\n') { next_start = i + 1; break; }
                    wc++;
                    if (wc >= cpl) { next_start = i + 1; break; }
                    if (i == note_len - 1) { next_start = note_len; } // no next line
                }
                if (next_start <= note_pos && note_pos < note_len) next_start = note_pos + 1;
                if (next_start <= note_len) {
                    // Find length of next line
                    int nl = 0;
                    for (int i = next_start; i < note_len; i++) {
                        if (notepad_buffer[i] == '\n') break;
                        nl++;
                        if (nl >= cpl) break;
                    }
                    int target_col = cur_col;
                    if (target_col > nl) target_col = nl;
                    if (shift) { if (note_sel < 0) note_sel = note_pos; }
                    else note_sel = -1;
                    note_pos = next_start + target_col;
                    if (note_pos > note_len) note_pos = note_len;
                }
                screen_dirty = true;
            } else if (c == '\b') {
                // Backspace
                if (note_sel >= 0) {
                    int s, e;
                    note_get_sel(&s, &e);
                    note_delete_range(s, e);
                } else if (ctrl) {
                    // Ctrl+Backspace: delete word left
                    int wl = word_left(note_pos);
                    note_delete_range(wl, note_pos);
                } else if (note_pos > 0) {
                    note_delete_range(note_pos - 1, note_pos);
                }
                screen_dirty = true;
            } else if (c == '\n') {
                // Enter: insert newline
                if (note_sel >= 0) {
                    int s, e;
                    note_get_sel(&s, &e);
                    note_delete_range(s, e);
                }
                char nl = '\n';
                note_insert(&nl, 1);
                screen_dirty = true;
            } else if (c == '\t') {
                // Tab: insert spaces based on tab size setting
                if (note_sel >= 0) {
                    int s, e;
                    note_get_sel(&s, &e);
                    note_delete_range(s, e);
                }
                note_insert("        ", setting_tab_size);
                screen_dirty = true;
            } else if (c >= ' ' && c < 0x7F) {
                // Printable character
                if (note_sel >= 0) {
                    int s, e;
                    note_get_sel(&s, &e);
                    note_delete_range(s, e);
                }
                note_insert(&c, 1);
                screen_dirty = true;
            }
        }

        // Input: Mouse Scroll Wheel (Notepad)
        if (current_app == APP_NOTE && current.scroll_delta != 0) {
            int scroll_amount = current.scroll_delta * 3; // 3 lines per notch
            // PS/2 scroll: positive = scroll down (away from user), negative = scroll up
            // But IntelliMouse Z axis: positive = scroll up, negative = scroll down
            // In QEMU the convention is: negative delta = scroll down
            note_scroll_y += scroll_amount;
            if (note_scroll_y < 0) note_scroll_y = 0;
            int max_scroll = note_total_lines - note_visible_lines;
            if (max_scroll < 0) max_scroll = 0;
            if (note_scroll_y > max_scroll) note_scroll_y = max_scroll;
            mouse_clear_scroll();
            screen_dirty = true;
        } else if (current.scroll_delta != 0) {
            mouse_clear_scroll(); // consume scroll in other views
        }

        // Scrollbar drag tracking (Notepad)
        if (current_app == APP_NOTE && note_sb_dragging) {
            if (current.left_button) {
                // Continue dragging
                int scrollbar_w = 12;
                int sb_y = 60;
                int sb_h = (int)fb.height - sb_y - 10;
                int thumb_h = (note_visible_lines * sb_h) / note_total_lines;
                if (thumb_h < 16) thumb_h = 16;
                int max_scroll = note_total_lines - note_visible_lines;
                if (max_scroll < 1) max_scroll = 1;
                int track_range = sb_h - thumb_h;
                if (track_range < 1) track_range = 1;
                int thumb_target_y = current.y - note_sb_drag_offset - sb_y;
                if (thumb_target_y < 0) thumb_target_y = 0;
                if (thumb_target_y > track_range) thumb_target_y = track_range;
                note_scroll_y = (thumb_target_y * max_scroll) / track_range;
                screen_dirty = true;
            } else {
                note_sb_dragging = false;
            }
        }

        // Input: Mouse Click
        if (current.left_button && !last_mouse.left_button) { // Clicked just now
             int clicked = get_clicked_app(current.x, current.y);
             if (clicked != -1 && clicked != current_app) {
                 current_app = clicked;
                 screen_dirty = true;
             }
             // Handle settings controls click
             if (current_app == APP_SETTINGS && current.x >= SIDEBAR_WIDTH) {
                 if (handle_settings_click(current.x, current.y)) {
                     screen_dirty = true;
                 }
             }

             // Handle notepad mouse click
             if (current_app == APP_NOTE && current.x >= SIDEBAR_WIDTH) {
                 int text_area_x = SIDEBAR_WIDTH + 20;
                 int text_area_y = 60;
                 int scrollbar_w = 12;
                 int sb_x = (int)fb.width - 20 - scrollbar_w;
                 int text_area_w = (int)fb.width - text_area_x - 20 - scrollbar_w;
                 int text_area_h = (int)fb.height - text_area_y - 10;
                 int char_w = 8;
                 int line_h = 12;
                 int max_cpl = text_area_w / char_w;
                 if (max_cpl < 1) max_cpl = 1;
                 int vis_lines = text_area_h / line_h;

                 if (current.x >= sb_x && current.x < sb_x + scrollbar_w
                     && current.y >= text_area_y && current.y < text_area_y + text_area_h
                     && note_total_lines > vis_lines) {
                     // Clicked on scrollbar track
                     int sb_h = text_area_h;
                     int thumb_h = (vis_lines * sb_h) / note_total_lines;
                     if (thumb_h < 16) thumb_h = 16;
                     int max_scroll = note_total_lines - vis_lines;
                     if (max_scroll < 1) max_scroll = 1;
                     int track_range = sb_h - thumb_h;
                     if (track_range < 1) track_range = 1;
                     int thumb_y = text_area_y + (note_scroll_y * track_range) / max_scroll;

                     if (current.y >= thumb_y && current.y < thumb_y + thumb_h) {
                         // Clicked on thumb - start drag
                         note_sb_dragging = true;
                         note_sb_drag_offset = current.y - thumb_y;
                     } else if (current.y < thumb_y) {
                         // Clicked above thumb - page up
                         note_scroll_y -= vis_lines;
                         if (note_scroll_y < 0) note_scroll_y = 0;
                     } else {
                         // Clicked below thumb - page down
                         note_scroll_y += vis_lines;
                         if (note_scroll_y > max_scroll) note_scroll_y = max_scroll;
                     }
                     screen_dirty = true;
                 } else if (current.x >= text_area_x && current.x < text_area_x + text_area_w
                            && current.y >= text_area_y && current.y < text_area_y + text_area_h) {
                     // Clicked in text area - position cursor
                     int click_col = (current.x - text_area_x) / char_w;
                     int click_line = (current.y - text_area_y) / line_h + note_scroll_y;

                     // Walk buffer to find position at (click_line, click_col)
                     int cur_line = 0, cur_col = 0;
                     int target_pos = note_len; // default: end
                     for (int i = 0; i <= note_len; i++) {
                         if (cur_line == click_line) {
                             if (i == note_len || cur_col >= click_col || notepad_buffer[i] == '\n') {
                                 target_pos = i;
                                 break;
                             }
                         } else if (cur_line > click_line) {
                             target_pos = i > 0 ? i - 1 : 0;
                             break;
                         }
                         if (i < note_len) {
                             if (notepad_buffer[i] == '\n') { cur_line++; cur_col = 0; }
                             else { cur_col++; if (cur_col >= max_cpl) { cur_line++; cur_col = 0; } }
                         }
                     }
                     note_pos = target_pos;
                     note_sel = -1;
                     screen_dirty = true;
                 }
             }
        }
        
        // Unified Redraw Logic
        bool mouse_moved = (current.x != last_mouse.x || current.y != last_mouse.y);
        
        if (screen_dirty || request_redraw || mouse_moved) {
            // 1. Hide Cursor (restore background at OLD position)
            for(int y=0; y<16; y++) {
                for(int x=0; x<12; x++) {
                     framebuffer_put_pixel(last_mouse.x + x, last_mouse.y + y, cursor_backing_store[y*12 + x]);
                }
            }

            // 2. Update Content (if needed)
            if (screen_dirty || request_redraw) {
                draw_sidebar();
                draw_content();  // Always redraw content fully for stability
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
