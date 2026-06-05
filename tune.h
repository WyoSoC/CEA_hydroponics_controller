#pragma once
#include "sensors.h"

// Guided auto-tune: dose one small bolus, record the response, fit a First-Order-
// Plus-Dead-Time model (two-point method), and compute PI gains via SIMC/IMC
// (robust, no-overshoot — important since pH is irreversible). Proposes gains for
// review; does not apply until tune_apply(). Runs as a non-blocking state machine.
void   tune_begin();
void   tune_tick(const SensorState& s);
bool   tune_start(char ch);   // 'p' = pH (acid), 'e' = EC (nutrients); false if can't start
void   tune_apply();          // write the proposed Kp/Ki into settings (user confirm)
void   tune_abort();
bool   tune_active();          // true while baselining/observing (control pauses)
String tune_status_json();
