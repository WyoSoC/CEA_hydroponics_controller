#include "control.h"
#include "config.h"
#include "commands.h"
#include "settings.h"
#include "tune.h"
#include <Arduino.h>

// Per-channel PI, dose-and-wait. Pump indices: 0 = acid, 1 = nutrient A, 2 = nutrient B.
static bool          autoOn = false;
static unsigned long lastProcessed = 0;
static unsigned long lastPhMs = 0, lastEcMs = 0;
static unsigned long acidWindowStart = 0;
static int           acidCount = 0;
static float         I_ph = 0.0f, I_ec = 0.0f;   // integrator terms (mL)

void control_begin() {}   // autonomous dosing always starts OFF on boot (fail-safe)

void control_set_auto(bool on) {
  autoOn = on;
  if (on) { I_ph = 0; I_ec = 0; lastPhMs = 0; lastEcMs = 0; }  // bumpless start
  Serial.printf("[control] autonomous PI dosing %s\n", on ? "ENABLED" : "disabled");
}
bool  control_auto_enabled() { return autoOn; }
float control_integral_ph() { return I_ph; }
float control_integral_ec() { return I_ec; }

static bool acidAllowed(unsigned long now) {
  if (now - acidWindowStart >= 3600000UL) { acidWindowStart = now; acidCount = 0; }
  return acidCount < MAX_ACID_DOSES_PER_HOUR;
}
static void enqueueDose(uint8_t idx, float ml) {
  Command c; c.type = CMD_DISPENSE; c.target = idx; c.value = ml; c.raw[0] = 0;
  commands_enqueue(c);
}

// One PI step. `e` is the error in the dosing direction (positive => dose needed).
// `dtMin` = settle interval in minutes. Returns the dose (mL), 0 if none.
// One-directional with conditional-integration anti-windup; integrator floored at 0.
static float piStep(float& I, float e, float Kp, float Ki, float dtMin,
                    float deadband, float maxDose) {
  if (e <= 0) { I += Ki * e * dtMin; if (I < 0) I = 0; return 0; }  // overshoot side: bleed I, no dose
  if (e <= deadband) return 0;                                       // in-band: hold I, no dose

  float Inew = I + Ki * e * dtMin; if (Inew < 0) Inew = 0;
  float u = Kp * e + Inew;
  if (u <= maxDose) I = Inew;            // accept integration only if not saturating (anti-windup)
  u = Kp * e + I;
  float dose = (u > maxDose) ? maxDose : u;
  return (dose < MIN_DOSE_ML) ? 0 : dose;
}

void control_tick(const SensorState& s) {
  if (tune_active()) return;     // don't fight the auto-tune test
  if (!autoOn) return;
  if (s.lastUpdateMs == 0 || s.lastUpdateMs == lastProcessed) return;  // only on a fresh reading
  lastProcessed = s.lastUpdateMs;

  const Settings& set = settings_get();
  unsigned long now = millis();

  // --- pH channel: acid (pump 0). error = pH - setpoint (positive => too high) ---
  if (s.phValid && (lastPhMs == 0 || now - lastPhMs >= (unsigned long)set.ph_min * 60000UL)) {
    float e = s.ph - set.ph_sp;
    float dose = piStep(I_ph, e, set.ph_kp, set.ph_ki, (float)set.ph_min, PH_DEADBAND, PH_MAX_DOSE_ML);
    if (dose > 0 && acidAllowed(now)) {
      enqueueDose(0, dose); acidCount++;
      Serial.printf("[control] pH %.2f (sp %.2f, I %.2f) -> acid %.2f mL\n", s.ph, set.ph_sp, I_ph, dose);
    }
    lastPhMs = now;
  }

  // --- EC channel: nutrients (pumps 1 & 2, equal). error = setpoint - EC (positive => too low) ---
  if (s.ecValid && (lastEcMs == 0 || now - lastEcMs >= (unsigned long)set.ec_min * 60000UL)) {
    float ec_mS = s.ec / 1000.0f;
    float e = set.ec_sp - ec_mS;
    float dose = piStep(I_ec, e, set.ec_kp, set.ec_ki, (float)set.ec_min, EC_DEADBAND, EC_MAX_DOSE_ML);
    if (dose > 0) {
      enqueueDose(1, dose); enqueueDose(2, dose);
      Serial.printf("[control] EC %.2f mS (sp %.2f, I %.2f) -> nutrients %.2f mL x2\n", ec_mS, set.ec_sp, I_ec, dose);
    }
    lastEcMs = now;
  }
}
