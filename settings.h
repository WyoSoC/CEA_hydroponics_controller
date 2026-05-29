#pragma once
#include <Arduino.h>

// Runtime-adjustable control settings, persisted in NVS (survive reboot + OTA).
// EC values are in mS/cm. PI gains + settle interval per channel.
struct Settings {
  float    ph_sp, ph_lo, ph_hi;   // setpoint + alarm thresholds
  float    ph_kp, ph_ki;          // PI gains (acid channel)
  uint32_t ph_min;                // settle/dose interval, minutes
  float    ec_sp, ec_lo, ec_hi;
  float    ec_kp, ec_ki;          // PI gains (nutrient channel)
  uint32_t ec_min;
};

void            settings_begin();              // load from NVS (defaults if empty)
const Settings& settings_get();
// Apply one UI key (phSp,phAlo,phAhi,phKp,phKi,phMin,ecSp,ecAlo,ecAhi,ecKp,ecKi,ecMin)
// with clamping; persists to NVS. Returns true if the key was recognized.
bool            settings_apply(const String& key, float val);
String          settings_json();
