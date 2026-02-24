#ifndef OLED_H
#define OLED_H

#include <Arduino.h>

// Initialize OLED (includes optional dim/low-contrast mode)
void init_oled();

// Top banner (static title)
void draw_banner();

// Main runtime UI
void draw_frame(bool line_EN[4], bool line_ST[4], bool at_ok, bool net_ok, bool armed);

// Boot splash UI (logo + minimal tests)
// If cooldownSec >= 0 it will show "CD XXs"
void draw_boot_screen(const char* header, bool at_ok, bool net_ok, int cooldownSec);

// Config mode UI
void config_oled();

// Legacy boot-step UI (kept for fallback)
void draw_boot_step(const char* currentTask, const char* history);

#endif
