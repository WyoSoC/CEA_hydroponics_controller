#pragma once
#include "sensors.h"

void console_begin();
void console_tick();                     // non-blocking serial line reader
bool console_streaming();                // whether to print each new reading
void print_reading_line(const SensorState& s);
