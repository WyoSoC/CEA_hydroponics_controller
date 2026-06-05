#pragma once
#include <Arduino.h>

// ===== Sensor port enable pins (Adafruit ESP32-S3 TFT Feather) =====
// Polarity is MIXED: RTD/Temp is active-HIGH, the rest active-LOW.
constexpr int EN_PH  = 12;   // LOW  = on
constexpr int EN_EC  = 11;   // LOW  = on
constexpr int EN_RTD = 9;    // HIGH = on
constexpr int EN_AUX = 10;   // LOW  = on

// ===== EZO I2C addresses =====
constexpr uint8_t ADDR_PH  = 99;
constexpr uint8_t ADDR_EC  = 100;
constexpr uint8_t ADDR_RTD = 102;
constexpr uint8_t ADDR_PUMP_PH_DOWN  = 0x38;   // pump 1
constexpr uint8_t ADDR_PUMP_PH_UP    = 0x39;   // pump 2
constexpr uint8_t ADDR_PUMP_NUTRIENT = 0x3A;   // pump 3

// ===== Poller timing (ms) =====
constexpr unsigned long EZO_READ_DELAY = 1000;  // wait for an "R" reading to be ready
constexpr unsigned long EZO_CMD_DELAY  = 300;   // wait for a quick command (e.g. "x")
constexpr unsigned long POLL_GAP       = 2000;  // idle window between full poll cycles
constexpr unsigned long EZO_CAL_DELAY  = 1300;  // wait for a calibration command to settle

// ===== Control model: per-channel PI, dose-and-wait, one-directional =====
//  Pump 1 = acid (only LOWERS pH; pH drifts up on its own; no base pump).
//  Pumps 2 & 3 = nutrients (equal dose each); only RAISE EC.
// Each channel runs a PI step once per settle interval (dose, then wait to mix
// and re-measure). Output clamps to [0, maxDose] (can't reverse). Defaults are
// used only when NVS is empty; live values settable in UI (settings.*). EC = mS/cm.
constexpr float    DEF_PH_SETPOINT = 5.9f;
constexpr float    DEF_PH_ALARM_LO = 5.7f;
constexpr float    DEF_PH_ALARM_HI = 6.7f;
constexpr float    DEF_PH_KP  = 1.0f;    // mL acid per pH unit of error
constexpr float    DEF_PH_KI  = 0.0f;    // mL per (pH*min); 0 = proportional-only (integral opt-in)
constexpr uint32_t DEF_PH_MIN = 10;      // pH dose-and-settle interval, minutes
constexpr float    DEF_EC_SETPOINT = 1.9f;   // mS/cm
constexpr float    DEF_EC_ALARM_LO = 0.0f;
constexpr float    DEF_EC_ALARM_HI = 2.3f;
constexpr float    DEF_EC_KP  = 2.0f;    // mL nutrient (each pump) per mS of error
constexpr float    DEF_EC_KI  = 0.0f;    // mL per (mS*min)
constexpr uint32_t DEF_EC_MIN = 30;      // EC dose-and-settle interval, minutes

// Bounds / safety (compile-time)
constexpr float PH_DEADBAND    = 0.05f;  // pH; no dose within this band of setpoint
constexpr float EC_DEADBAND    = 0.03f;  // mS/cm
constexpr float MIN_DOSE_ML    = 0.10f;  // skip unreliable micro-doses below this
constexpr float PH_MAX_DOSE_ML = 2.0f;   // max acid per cycle
constexpr float EC_MAX_DOSE_ML = 5.0f;   // max nutrient per cycle (per pump)
constexpr int   MAX_ACID_DOSES_PER_HOUR = 6;   // acid runaway cap

// ===== Sensor sanity ranges (readings outside -> treated as faults) =====
constexpr float PH_MIN = 2.0f,  PH_MAX = 12.0f;
constexpr float EC_MIN = 0.0f,  EC_MAX = 50000.0f;
constexpr float TEMP_DEFAULT = 25.0f;   // compensation fallback if RTD read fails

// ===== Sensor read robustness =====
constexpr int           SENSOR_READ_RETRIES = 2;       // re-issue a read this many times on I2C failure
constexpr unsigned long DOSE_SETTLE_MS      = 3000;    // pause sampling this long after a dose (EMI settle)
constexpr int           BUS_RECOVER_CYCLES  = 5;       // consecutive all-sensors-failed cycles -> reinit I2C

// ===== Local data logging (history ring buffer) =====
constexpr int    LOG_INTERVAL_S   = 10;                                // default seconds between samples
constexpr int    LOG_INTERVAL_MIN = 1;                                 // user-settable range (s)
constexpr int    LOG_INTERVAL_MAX = 3600;
constexpr int    LOG_DAYS       = 7;                                   // target history window
constexpr size_t LOG_CAPACITY   = (size_t)LOG_DAYS * 24 * 3600 / LOG_INTERVAL_S;  // 60480
// 8 bytes/sample -> ~473 KB for 7 days; lives in PSRAM (needs Tools->PSRAM enabled).
// Falls back to ~1 day in internal RAM if PSRAM is unavailable. Volatile (cleared on reboot).
constexpr size_t LOG_FALLBACK_CAP = (size_t)1 * 24 * 3600 / LOG_INTERVAL_S;       // 8640

// ===== Auto-tune (step-response FOPDT identification -> SIMC/IMC gains) =====
constexpr float         TUNE_PH_DOSE_ML    = 0.5f;     // acid bolus for pH ID (small; pH is irreversible)
constexpr float         TUNE_EC_DOSE_ML    = 1.0f;     // nutrient bolus (per pump) for EC ID
constexpr unsigned long TUNE_BASELINE_MS   = 30000;    // average a baseline for ~30 s first
constexpr unsigned long TUNE_MAX_MS        = 600000;   // give up observing after 10 min
constexpr unsigned long TUNE_MIN_OBS_MS    = 60000;    // don't declare "settled" before this (dead time)
constexpr float         TUNE_MIN_CHANGE_PH = 0.10f;    // need >= this much movement to be valid
constexpr float         TUNE_MIN_CHANGE_EC = 0.05f;    // mS/cm
constexpr float         TUNE_LAMBDA_FACTOR = 2.0f;     // lambda = factor * dead-time (conservative)

// ===== Alarm notifications =====
constexpr unsigned long NOTIFY_RENOTIFY_MS = 30UL * 60UL * 1000UL;  // re-send while an alarm persists

// ===== ThingSpeak cloud upload =====
constexpr int TS_MIN_INTERVAL_S     = 15;    // free-tier minimum between updates
constexpr int TS_DEFAULT_INTERVAL_S = 60;

// ===== Web control guardrails =====
constexpr float    MANUAL_MAX_DOSE_ML = 25.0f;  // clamp on any single manual dispense
constexpr uint32_t DEFAULT_UNLOCK_MIN = 60;     // default public-control unlock duration
constexpr uint32_t MAX_UNLOCK_MIN     = 240;    // cap on unlock duration
