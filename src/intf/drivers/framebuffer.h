#pragma once
#include <stdint.h>
#include <stddef.h>

struct Framebuffer {
    void* base_address;
    size_t buffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
};

void framebuffer_init(void* multiboot_tag);
void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t framebuffer_get_pixel(uint32_t x, uint32_t y);
void framebuffer_clear(uint32_t color);
void framebuffer_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void framebuffer_draw_cursor(uint32_t x, uint32_t y);
void framebuffer_swap(void);
void framebuffer_blit_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
