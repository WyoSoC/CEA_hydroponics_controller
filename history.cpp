#include "history.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <Preferences.h>

// 8-byte compact record. Time is implicit (samples are g_interval seconds apart).
struct Rec { int16_t ph; uint16_t ec; int16_t temp; uint8_t flags; uint8_t pad; };
// flags: bit0 phOk, bit1 ecOk, bit2 tempOk

static Rec*   buf   = nullptr;
static size_t cap   = 0;     // allocated capacity
static size_t count = 0;     // valid samples
static size_t head  = 0;     // next write index
static bool   inPsram = false;
static unsigned long lastMs = 0;
static int    g_interval = LOG_INTERVAL_S;   // runtime sample interval (s)
static Preferences hp;

void history_begin() {
  hp.begin("loghz", false);
  int s = hp.getInt("s", LOG_INTERVAL_S);
  g_interval = (s >= LOG_INTERVAL_MIN && s <= LOG_INTERVAL_MAX) ? s : LOG_INTERVAL_S;

  if (psramFound()) {
    buf = (Rec*)heap_caps_malloc(LOG_CAPACITY * sizeof(Rec), MALLOC_CAP_SPIRAM);
    if (buf) { cap = LOG_CAPACITY; inPsram = true; }
  }
  if (!buf) {                                   // fallback: smaller buffer in internal RAM
    buf = (Rec*)malloc(LOG_FALLBACK_CAP * sizeof(Rec));
    if (buf) cap = LOG_FALLBACK_CAP;
  }
  Serial.printf("[history] %u samples in %s (%u KB), %d s interval, ~%.1f day max span\n",
                (unsigned)cap, inPsram ? "PSRAM" : "internal RAM",
                (unsigned)(cap * sizeof(Rec) / 1024), g_interval,
                cap * (double)g_interval / 86400.0);
}

void history_tick(const SensorState& s) {
  if (!buf || cap == 0) return;
  unsigned long now = millis();
  if (lastMs != 0 && now - lastMs < (unsigned long)g_interval * 1000UL) return;
  lastMs = now;

  Rec r;
  r.ph    = s.phValid   ? (int16_t)lroundf(s.ph * 100.0f) : 0;
  r.ec    = s.ecValid   ? (uint16_t)(s.ec < 0 ? 0 : (s.ec > 65535 ? 65535 : s.ec)) : 0;
  r.temp  = s.tempValid ? (int16_t)lroundf(s.tempC * 100.0f) : 0;
  r.flags = (s.phValid ? 1 : 0) | (s.ecValid ? 2 : 0) | (s.tempValid ? 4 : 0);
  r.pad   = 0;
  buf[head] = r;
  head = (head + 1) % cap;
  if (count < cap) count++;
}

size_t history_count()    { return count; }
size_t history_capacity() { return cap; }
bool   history_in_psram() { return inPsram; }
int    history_interval_s() { return g_interval; }

bool history_set_interval(int s) {
  if (s < LOG_INTERVAL_MIN || s > LOG_INTERVAL_MAX) return false;
  if (s != g_interval) {
    g_interval = s; hp.putInt("s", s);
    count = 0; head = 0; lastMs = 0;            // implicit timestamps -> clear on interval change
    Serial.printf("[history] interval -> %d s (buffer cleared)\n", s);
  }
  return true;
}

void history_clear() { count = 0; head = 0; lastMs = 0; Serial.println(F("[history] cleared")); }

static inline Rec& at(size_t i) {              // i = 0 oldest .. count-1 newest
  return buf[(head + cap - count + i) % cap];
}

String history_json(size_t maxPoints) {
  String out; out.reserve(maxPoints * 22 + 192);
  size_t bytes = cap * sizeof(Rec);
  int pct = cap ? (int)((count * 100) / cap) : 0;
  out += "{\"interval\":"; out += g_interval;
  out += ",\"count\":";    out += count;
  out += ",\"cap\":";      out += cap;
  out += ",\"bytes\":";    out += bytes;
  out += ",\"pct\":";      out += pct;
  out += ",\"psram\":";    out += (inPsram ? "true" : "false");
  out += ",\"spanMaxS\":"; out += (unsigned long)(cap * (size_t)g_interval);
  if (count == 0 || !buf) { out += ",\"n\":0,\"ph\":[],\"ec\":[],\"temp\":[],\"age\":[]}"; return out; }

  size_t stride = (count > maxPoints) ? (count + maxPoints - 1) / maxPoints : 1;
  String ph = "[", ec = "[", tp = "[", ag = "[";
  bool first = true; size_t n = 0;
  for (size_t j = 0; j < count; j += stride) {
    Rec& r = at(j);
    if (!first) { ph += ","; ec += ","; tp += ","; ag += ","; }
    first = false; n++;
    ag += String((long)(count - 1 - j) * g_interval);          // seconds ago (newest ~0)
    ph += (r.flags & 1) ? String(r.ph / 100.0, 2)  : String("null");
    ec += (r.flags & 2) ? String(r.ec / 1000.0, 3) : String("null");  // mS/cm
    tp += (r.flags & 4) ? String(r.temp / 100.0, 1): String("null");
  }
  ph += "]"; ec += "]"; tp += "]"; ag += "]";
  out += ",\"n\":"; out += n;
  out += ",\"ph\":";  out += ph;
  out += ",\"ec\":";  out += ec;
  out += ",\"temp\":";out += tp;
  out += ",\"age\":"; out += ag;
  out += "}";
  return out;
}

size_t history_csv_line(size_t i, char* out, size_t max) {
  if (i >= count) return 0;
  Rec& r = at(i);
  long age = (long)(count - 1 - i) * g_interval;
  char ph[10] = "", ec[12] = "", tp[10] = "";
  if (r.flags & 1) snprintf(ph, sizeof(ph), "%.2f", r.ph / 100.0);
  if (r.flags & 2) snprintf(ec, sizeof(ec), "%.3f", r.ec / 1000.0);
  if (r.flags & 4) snprintf(tp, sizeof(tp), "%.1f", r.temp / 100.0);
  return snprintf(out, max, "%ld,%s,%s,%s\n", age, ph, ec, tp);
}
