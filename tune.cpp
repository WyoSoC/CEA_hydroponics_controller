#include "tune.h"
#include "config.h"
#include "commands.h"
#include "settings.h"
#include "control.h"
#include <Arduino.h>
#include <math.h>

enum TState { T_IDLE, T_BASELINE, T_OBSERVE, T_DONE, T_ERROR };

static TState        state = T_IDLE;
static char          chan = 0;            // 'p' or 'e'
static unsigned long t0 = 0, doseMs = 0, lastProc = 0;
static double        baseSum = 0; static int baseCnt = 0;
static float         baseline = 0, dose = 0;

static const int  MAXP = 256;
static uint16_t   tsec[MAXP];             // seconds since dose
static float      tval[MAXP];
static int        np = 0;

static float  rK = 0, rL = 0, rT = 0, rKp = 0, rKi = 0;   // results (L,T in seconds)
static char   msg[56] = "";

void tune_begin() { state = T_IDLE; }
bool tune_active() { return state == T_BASELINE || state == T_OBSERVE; }

static void fail(const char* m) { state = T_ERROR; snprintf(msg, sizeof(msg), "%s", m); }

static void dosePump(uint8_t idx, float ml) {
  Command c; c.type = CMD_DISPENSE; c.target = idx; c.value = ml; c.raw[0] = 0;
  commands_enqueue(c);
}

bool tune_start(char ch) {
  if (tune_active()) return false;
  if (ch != 'p' && ch != 'e') return false;
  control_set_auto(false);                 // stop PI so it doesn't fight the test
  chan = ch; state = T_BASELINE; t0 = millis();
  baseSum = 0; baseCnt = 0; np = 0; lastProc = 0;
  snprintf(msg, sizeof(msg), "measuring baseline");
  return true;
}

void tune_abort() { state = T_IDLE; snprintf(msg, sizeof(msg), "aborted"); }

void tune_apply() {
  if (state != T_DONE) return;
  settings_apply(chan == 'p' ? "phKp" : "ecKp", rKp);
  settings_apply(chan == 'p' ? "phKi" : "ecKi", rKi);
  state = T_IDLE; snprintf(msg, sizeof(msg), "applied");
}

// first time (s since dose) the trajectory reaches baseline + frac*delta; -1 if never
static float crossTime(float frac, float delta) {
  float target = baseline + frac * delta;
  for (int i = 0; i < np; i++) {
    bool crossed = (delta > 0) ? (tval[i] >= target) : (tval[i] <= target);
    if (crossed) {
      if (i == 0) return tsec[0];
      float v0 = tval[i - 1], v1 = tval[i];
      if (v1 == v0) return tsec[i];
      float f = (target - v0) / (v1 - v0);
      return tsec[i - 1] + f * (tsec[i] - tsec[i - 1]);
    }
  }
  return -1;
}

static void finalize() {
  if (np < 4) { fail("too few samples"); return; }
  float settledVal = (tval[np - 1] + tval[np - 2] + tval[np - 3]) / 3.0f;   // settled value
  float delta = settledVal - baseline;                                     // signed (pH down, EC up)
  float minChange = (chan == 'p') ? TUNE_MIN_CHANGE_PH : TUNE_MIN_CHANGE_EC;
  if (fabs(delta) < minChange) { fail("no response; larger dose / check pump"); return; }

  float t1 = crossTime(0.283f, delta);
  float t2 = crossTime(0.632f, delta);
  if (t1 < 0 || t2 < 0 || t2 <= t1) { fail("could not fit response"); return; }

  float T_s = 1.5f * (t2 - t1);
  float L_s = t2 - T_s; if (L_s < 0) L_s = 0;
  float K   = fabs(delta) / dose;            // reading-change per (per-pump) mL

  // SIMC (units cancel in Kp; integral time in MINUTES to match control's dt)
  float Tm = T_s / 60.0f, Lm = L_s / 60.0f;
  float lam = TUNE_LAMBDA_FACTOR * Lm; if (lam < 0.2f * Tm) lam = 0.2f * Tm;   // floor lambda
  float Kp = (1.0f / K) * (Tm / (lam + Lm));
  float tauI = Tm; if (tauI > 4.0f * (lam + Lm)) tauI = 4.0f * (lam + Lm);
  float Ki = (tauI > 0) ? Kp / tauI : 0;

  if (Kp < 0) Kp = 0; if (Kp > 50) Kp = 50;
  if (Ki < 0) Ki = 0; if (Ki > 50) Ki = 50;

  rK = K; rL = L_s; rT = T_s; rKp = Kp; rKi = Ki;
  state = T_DONE; snprintf(msg, sizeof(msg), "done");
}

void tune_tick(const SensorState& s) {
  if (state != T_BASELINE && state != T_OBSERVE) return;
  bool fresh = (s.lastUpdateMs != 0 && s.lastUpdateMs != lastProc);
  if (fresh) lastProc = s.lastUpdateMs;
  unsigned long now = millis();
  float val   = (chan == 'p') ? s.ph : s.ec / 1000.0f;     // EC in mS
  bool  valid = (chan == 'p') ? s.phValid : s.ecValid;

  if (state == T_BASELINE) {
    if (fresh && valid) { baseSum += val; baseCnt++; }
    if (now - t0 >= TUNE_BASELINE_MS) {
      if (baseCnt < 1) { fail("no valid baseline reading"); return; }
      baseline = baseSum / baseCnt;
      dose = (chan == 'p') ? TUNE_PH_DOSE_ML : TUNE_EC_DOSE_ML;
      if (chan == 'p') dosePump(0, dose);
      else { dosePump(1, dose); dosePump(2, dose); }     // nutrients: both pumps, equal
      doseMs = millis(); np = 0;
      state = T_OBSERVE;
      snprintf(msg, sizeof(msg), "dosed %.2f mL, observing", dose);
    }
    return;
  }

  // T_OBSERVE
  if (fresh && valid && np < MAXP) { tsec[np] = (uint16_t)((now - doseMs) / 1000); tval[np] = val; np++; }
  float moved = (np > 0) ? fabs(tval[np - 1] - baseline) : 0;
  float minChange = (chan == 'p') ? TUNE_MIN_CHANGE_PH : TUNE_MIN_CHANGE_EC;
  float eps = (chan == 'p') ? 0.02f : 0.01f;
  bool flat = (np >= 8) && (fabs(tval[np - 1] - tval[np - 5]) < eps);
  bool settled = (now - doseMs > TUNE_MIN_OBS_MS) && (moved >= minChange) && flat;
  if (settled || now - doseMs >= TUNE_MAX_MS) finalize();
}

String tune_status_json() {
  const char* st = state == T_IDLE ? "idle" : state == T_BASELINE ? "baseline"
                 : state == T_OBSERVE ? "observe" : state == T_DONE ? "done" : "error";
  unsigned long el = (state == T_BASELINE) ? (millis() - t0) / 1000
                   : (state == T_OBSERVE)  ? (millis() - doseMs) / 1000 : 0;
  char b[260];
  snprintf(b, sizeof(b),
    "{\"state\":\"%s\",\"ch\":\"%c\",\"msg\":\"%s\",\"elapsed\":%lu,\"n\":%d,"
    "\"K\":%.3f,\"L\":%.0f,\"T\":%.0f,\"kp\":%.2f,\"ki\":%.3f}",
    st, chan ? chan : '-', msg, el, np, rK, rL, rT, rKp, rKi);
  return String(b);
}
