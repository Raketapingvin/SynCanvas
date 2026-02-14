#pragma once
#include <stdint.h>

void text_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg);
void text_draw_string(const char* str, int x, int y, uint32_t fg, uint32_t bg);
void text_draw_char_scaled(char c, int x, int y, uint32_t fg, uint32_t bg, int scale);
void text_draw_string_scaled(const char* str, int x, int y, uint32_t fg, uint32_t bg, int scale);
void graphics_draw_bitmap(const uint8_t* bitmap, int x, int y, int w, int h);
