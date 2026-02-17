#include "drivers/framebuffer.h"

struct Framebuffer fb = {0};

// Double buffering: all drawing goes to back_buffer, then swap copies to screen
// Placed at a fixed address (4MB) to avoid bloating BSS
static uint8_t* back_buffer = (uint8_t*)0x400000;

// Dirty rectangle tracking: only swap changed region
static uint32_t dirty_x1, dirty_y1, dirty_x2, dirty_y2;
static int dirty = 0;

static void dirty_reset(void) {
    dirty = 0;
    dirty_x1 = 0xFFFFFFFF;
    dirty_y1 = 0xFFFFFFFF;
    dirty_x2 = 0;
    dirty_y2 = 0;
}

static void dirty_mark(uint32_t x, uint32_t y) {
    dirty = 1;
    if (x < dirty_x1) dirty_x1 = x;
    if (y < dirty_y1) dirty_y1 = y;
    if (x > dirty_x2) dirty_x2 = x;
    if (y > dirty_y2) dirty_y2 = y;
}

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_framebuffer {
    struct multiboot_tag common;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t type;
    uint8_t reserved;
};

void framebuffer_init(void* mboot_addr) {
    struct multiboot_tag* tag;
    uint32_t* addr = (uint32_t*) mboot_addr;
    
    // Skip first 8 bytes (total_size, reserved)
    tag = (struct multiboot_tag*)(addr + 2);

    while (tag->type != 0) {
        if (tag->type == 8) { // Framebuffer info tag
             struct multiboot_tag_framebuffer* fb_tag = (struct multiboot_tag_framebuffer*) tag;
             fb.base_address = (void*) fb_tag->addr;
             fb.width = fb_tag->width;
             fb.height = fb_tag->height;
             fb.pitch = fb_tag->pitch;
             fb.bpp = fb_tag->bpp;
             fb.buffer_size = fb.pitch * fb.height;
             dirty_reset();
             return;
        }
        
        // Next tag (8-byte aligned)
        tag = (struct multiboot_tag*) ((uint8_t*)tag + ((tag->size + 7) & ~7));
    }
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb.width || y >= fb.height || fb.base_address == 0) return;
    
    // Write to back buffer instead of directly to screen
    uint32_t* pixel = (uint32_t*)(back_buffer + y * fb.pitch + x * 4);
    *pixel = color;
    dirty_mark(x, y);
}

uint32_t framebuffer_get_pixel(uint32_t x, uint32_t y) {
    if (x >= fb.width || y >= fb.height || fb.base_address == 0) return 0;
    
    // Read from back buffer
    uint32_t* pixel = (uint32_t*)(back_buffer + y * fb.pitch + x * 4);
    return *pixel;
}

void framebuffer_swap(void) {
    if (fb.base_address == 0 || fb.buffer_size == 0 || !dirty) return;

    // Only copy the dirty rectangle to VRAM
    uint32_t x = dirty_x1;
    uint32_t y = dirty_y1;
    uint32_t w = dirty_x2 - dirty_x1 + 1;
    uint32_t h = dirty_y2 - dirty_y1 + 1;

    for (uint32_t row = y; row < y + h; row++) {
        uint64_t* dst = (uint64_t*)((uint8_t*)fb.base_address + row * fb.pitch + (x & ~1) * 4);
        uint64_t* src = (uint64_t*)(back_buffer + row * fb.pitch + (x & ~1) * 4);
        uint32_t qwords = ((w + (x & 1) + 1) / 2);
        for (uint32_t i = 0; i < qwords; i++) {
            dst[i] = src[i];
        }
    }

    dirty_reset();
}

void framebuffer_blit_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (fb.base_address == 0) return;

    // Clamp to screen bounds
    if (x >= fb.width || y >= fb.height) return;
    if (x + w > fb.width) w = fb.width - x;
    if (y + h > fb.height) h = fb.height - y;

    // Copy only the specified rectangle from back buffer to video memory
    for (uint32_t row = y; row < y + h; row++) {
        uint32_t* dst = (uint32_t*)((uint8_t*)fb.base_address + row * fb.pitch + x * 4);
        uint32_t* src = (uint32_t*)(back_buffer + row * fb.pitch + x * 4);
        for (uint32_t col = 0; col < w; col++) {
            dst[col] = src[col];
        }
    }
}

void framebuffer_clear(uint32_t color) {
    if (fb.base_address == 0) return;
    
    // Naive clear
    for (uint32_t y = 0; y < fb.height; y++) {
        for (uint32_t x = 0; x < fb.width; x++) {
            framebuffer_put_pixel(x, y, color);
        }
    }
}

void framebuffer_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            framebuffer_put_pixel(x + j, y + i, color);
        }
    }
}

void framebuffer_draw_cursor(uint32_t x, uint32_t y) {
    // Arrow cursor (12x16) - drawn as filled triangle with border
    // Row by row: width increases as we go down, with a notch for the tail
    static const uint8_t arrow[16][12] = {
        {1,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0},
        {1,2,2,2,2,2,2,2,1,0,0,0},
        {1,2,2,2,2,2,2,2,2,1,0,0},
        {1,2,2,2,2,2,1,1,1,1,1,0},
        {1,2,2,1,2,2,1,0,0,0,0,0},
        {1,2,1,0,1,2,2,1,0,0,0,0},
        {1,1,0,0,1,2,2,1,0,0,0,0},
        {1,0,0,0,0,1,2,2,1,0,0,0},
        {0,0,0,0,0,1,1,1,1,0,0,0},
    };
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 12; col++) {
            if (arrow[row][col] == 1) {
                framebuffer_put_pixel(x + col, y + row, 0xFF000000); // Black border
            } else if (arrow[row][col] == 2) {
                framebuffer_put_pixel(x + col, y + row, 0xFFFFFFFF); // White fill
            }
        }
    }
}
