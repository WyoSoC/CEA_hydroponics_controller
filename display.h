#pragma once
#include "sensors.h"

// On-board 240x135 ST7789 TFT: shows hostname, IP, live pH/EC/temp, lock/auto
// status, and the last pump action. Small fonts. Redraws ~1 Hz, flicker-free
// (opaque-background text overwrites in place; no full-screen clear).
void display_begin();
void display_tick(const SensorState& s);
