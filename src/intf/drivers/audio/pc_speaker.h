#pragma once
#include <stdint.h>

void pc_speaker_init();
void pc_speaker_beep(uint32_t freq, uint32_t duration_ms);
void pc_speaker_play_sound(uint32_t freq);
void pc_speaker_stop_sound();
void pc_speaker_startup_sound();
