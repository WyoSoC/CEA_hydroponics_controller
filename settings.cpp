#include "settings.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;
static Settings S;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void settings_begin() {
  prefs.begin("hydro", false);
  S.ph_sp  = prefs.getFloat("phSp",  DEF_PH_SETPOINT);
  S.ph_lo  = prefs.getFloat("phAlo", DEF_PH_ALARM_LO);
  S.ph_hi  = prefs.getFloat("phAhi", DEF_PH_ALARM_HI);
  S.ph_kp  = prefs.getFloat("phKp",  DEF_PH_KP);
  S.ph_ki  = prefs.getFloat("phKi",  DEF_PH_KI);
  S.ph_min = prefs.getUInt ("phMin", DEF_PH_MIN);
  S.ec_sp  = prefs.getFloat("ecSp",  DEF_EC_SETPOINT);
  S.ec_lo  = prefs.getFloat("ecAlo", DEF_EC_ALARM_LO);
  S.ec_hi  = prefs.getFloat("ecAhi", DEF_EC_ALARM_HI);
  S.ec_kp  = prefs.getFloat("ecKp",  DEF_EC_KP);
  S.ec_ki  = prefs.getFloat("ecKi",  DEF_EC_KI);
  S.ec_min = prefs.getUInt ("ecMin", DEF_EC_MIN);
}

const Settings& settings_get() { return S; }

bool settings_apply(const String& key, float val) {
  if      (key == "phSp")  { S.ph_sp  = clampf(val, 3, 9);     prefs.putFloat("phSp",  S.ph_sp); }
  else if (key == "phAlo") { S.ph_lo  = clampf(val, 3, 9);     prefs.putFloat("phAlo", S.ph_lo); }
  else if (key == "phAhi") { S.ph_hi  = clampf(val, 3, 9);     prefs.putFloat("phAhi", S.ph_hi); }
  else if (key == "phKp")  { S.ph_kp  = clampf(val, 0, 50);    prefs.putFloat("phKp",  S.ph_kp); }
  else if (key == "phKi")  { S.ph_ki  = clampf(val, 0, 50);    prefs.putFloat("phKi",  S.ph_ki); }
  else if (key == "phMin") { S.ph_min = (uint32_t)clampf(val, 1, 1440); prefs.putUInt("phMin", S.ph_min); }
  else if (key == "ecSp")  { S.ec_sp  = clampf(val, 0, 5);     prefs.putFloat("ecSp",  S.ec_sp); }
  else if (key == "ecAlo") { S.ec_lo  = clampf(val, 0, 5);     prefs.putFloat("ecAlo", S.ec_lo); }
  else if (key == "ecAhi") { S.ec_hi  = clampf(val, 0, 5);     prefs.putFloat("ecAhi", S.ec_hi); }
  else if (key == "ecKp")  { S.ec_kp  = clampf(val, 0, 50);    prefs.putFloat("ecKp",  S.ec_kp); }
  else if (key == "ecKi")  { S.ec_ki  = clampf(val, 0, 50);    prefs.putFloat("ecKi",  S.ec_ki); }
  else if (key == "ecMin") { S.ec_min = (uint32_t)clampf(val, 1, 1440); prefs.putUInt("ecMin", S.ec_min); }
  else return false;
  return true;
}

String settings_json() {
  char b[288];
  snprintf(b, sizeof(b),
    "{\"phSp\":%.2f,\"phAlo\":%.2f,\"phAhi\":%.2f,\"phKp\":%.2f,\"phKi\":%.3f,\"phMin\":%u,"
    "\"ecSp\":%.2f,\"ecAlo\":%.2f,\"ecAhi\":%.2f,\"ecKp\":%.2f,\"ecKi\":%.3f,\"ecMin\":%u}",
    S.ph_sp, S.ph_lo, S.ph_hi, S.ph_kp, S.ph_ki, (unsigned)S.ph_min,
    S.ec_sp, S.ec_lo, S.ec_hi, S.ec_kp, S.ec_ki, (unsigned)S.ec_min);
  return String(b);
}
