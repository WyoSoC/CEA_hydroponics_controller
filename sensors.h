#pragma once
#include <Arduino.h>

// Latest sensor snapshot. *Valid flags mean: received OK AND within sanity range.
struct SensorState {
  float ph;
  float ec;
  float tempC;
  bool  phValid;
  bool  ecValid;
  bool  tempValid;
  unsigned long lastUpdateMs;   // millis() of the last completed cycle (0 = none yet)
};

void sensors_begin();
void sensors_tick();             // non-blocking; call every loop()
bool sensors_idle();             // true between poll cycles -> safe to run a queued I2C command
bool sensors_consume_new();      // returns true once after each completed reading
SensorState sensors_snapshot();  // copy of the latest state (safe to read anytime)

void sensors_pause(unsigned long ms);  // defer the next poll cycle (e.g., to let a dose settle)
int  sensors_fail_ph();          // consecutive read failures (0 when healthy)
int  sensors_fail_ec();
int  sensors_fail_temp();
