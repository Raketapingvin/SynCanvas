#include "drivers/display/text.h"
#include "drivers/display/font.h"
#include "drivers/framebuffer.h"

void text_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 127) return;

    // Get the font logic
    const uint8_t* glyph = font8x8_basic[c - 32];

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            // Check top bit (0x80)
            if (bits & 0x80) {
                framebuffer_put_pixel(x + col, y + row, fg);
            } else {
                // If bg is NOT transparent (let's say 0 is transparent for now, or just always draw if we want solid box)
                // For now, let's assume transparent BG if bg == 0, else draw
                if (bg != 0) {
                    framebuffer_put_pixel(x + col, y + row, bg);
                }
            }
            bits <<= 1;
        }
    }
}

void text_draw_string(const char* str, int x, int y, uint32_t fg, uint32_t bg) {
    int cur_x = x;
    int cur_y = y;
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 8;
        } else {
            text_draw_char(*str, cur_x, cur_y, fg, bg);
            cur_x += 8;
        }
        str++;
    }
}

void text_draw_char_scaled(char c, int x, int y, uint32_t fg, uint32_t bg, int scale) {
    if (c < 32 || c > 127) return;

    // Get the font logic
    const uint8_t* glyph = font8x8_basic[c - 32];

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            // Check top bit (0x80)
            if (bits & 0x80) {
                // Draw pixel scaled
                for(int dy = 0; dy < scale; dy++) {
                    for(int dx = 0; dx < scale; dx++) {
                        framebuffer_put_pixel(x + (col * scale) + dx, y + (row * scale) + dy, fg);
                    }
                }
            } else {
                if (bg != 0) {
                     for(int dy = 0; dy < scale; dy++) {
                        for(int dx = 0; dx < scale; dx++) {
                            framebuffer_put_pixel(x + (col * scale) + dx, y + (row * scale) + dy, bg);
                        }
                    }
                }
            }
            bits <<= 1;
        }
    }
}

void text_draw_string_scaled(const char* str, int x, int y, uint32_t fg, uint32_t bg, int scale) {
    int cur_x = x;
    int cur_y = y;
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            cur_y += 8 * scale;
        } else {
            text_draw_char_scaled(*str, cur_x, cur_y, fg, bg, scale);
            cur_x += 8 * scale;
        }
        str++;
    }
}

// Basic bitmap drawer (row-major, 1 byte per pixel? Or packed?)
// User asked for "image rendering". Loading standard formats is hard.
// Let's implement a raw RGBA bitmap drawer.
void graphics_draw_bitmap(const uint8_t* bitmap, int x, int y, int w, int h) {
    const uint32_t* pixels = (const uint32_t*)bitmap;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            framebuffer_put_pixel(x + col, y + row, pixels[row * w + col]);
        }
    }
}
