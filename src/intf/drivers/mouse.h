#pragma once
#include <stdint.h>
#include <drivers/framebuffer.h>
struct MouseState {
    int32_t x;
    int32_t y;
    uint8_t left_button;
    uint8_t right_button;
    int8_t scroll_delta; // New field for Z-axis
};

void mouse_init();
void mouse_enable_irq();
void mouse_update();
void mouse_handle_packet();
struct MouseState mouse_get_state();
void mouse_clear_scroll();
extern volatile int mouse_speed_setting;
