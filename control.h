#pragma once
#include "sensors.h"

// Autonomous dosing: per-channel PI in a dose-and-wait loop. Each channel acts
// once per its settle interval, output clamped one-directional to [0, maxDose]
// (acid lowers pH; nutrients raise EC). Anti-windup + sanity gating; default OFF.
void  control_begin();
void  control_tick(const SensorState& s);
void  control_set_auto(bool on);     // resets integrators (bumpless) on enable
bool  control_auto_enabled();
float control_integral_ph();         // current pH integrator term (for UI/tuning)
float control_integral_ec();         // current EC integrator term
