#include "access.h"
#include "config.h"
#include "secrets.h"
#include <string.h>

static bool          locked      = true;   // safe default
static unsigned long unlockUntil = 0;
static bool          changed     = false;

struct LogEntry { unsigned long ms; char who[8]; char action[14]; char detail[40]; };
static const int LOG_N = 24;
static LogEntry logs[LOG_N];
static int logHead = 0, logCount = 0;

void access_begin() { locked = true; unlockUntil = 0; changed = true; logHead = 0; logCount = 0; }

bool access_consume_lock_changed() { if (changed) { changed = false; return true; } return false; }

bool access_secret_ok(AsyncWebServerRequest* req) {
  const AsyncWebHeader* h = req->getHeader("X-Proxy-Secret");
  return h && h->value() == PROXY_SECRET;
}

Origin access_origin(AsyncWebServerRequest* req) {
  if (access_secret_ok(req)) {                       // came through our proxy
    const AsyncWebHeader* h = req->getHeader("X-Access");
    if (h && h->value() == "tailnet") return ORIGIN_TAILNET;
    return ORIGIN_PUBLIC;                             // proxied but not tailnet -> least privilege
  }
  return ORIGIN_TAILNET;                              // direct LAN/tailnet (not publicly routable)
}

bool access_locked() { return locked; }

uint32_t access_unlock_remaining_s() {
  if (locked) return 0;
  long rem = (long)(unlockUntil - millis());
  return rem > 0 ? (uint32_t)(rem / 1000) : 0;
}

void access_lock()                  { locked = true;  unlockUntil = 0; changed = true; }
void access_unlock(uint32_t minutes){ locked = false; unlockUntil = millis() + (unsigned long)minutes * 60000UL; changed = true; }

void access_tick() {
  if (!locked && (long)(millis() - unlockUntil) >= 0) {
    locked = true; unlockUntil = 0; changed = true;
    Serial.println(F("[access] auto-relocked (unlock expired)"));
  }
}

bool access_can_control(Origin o)     { return o == ORIGIN_TAILNET || !locked; }
bool access_can_toggle_lock(Origin o) { return o == ORIGIN_TAILNET; }

void access_log(Origin o, const char* action, const char* detail) {
  LogEntry& e = logs[logHead];
  e.ms = millis();
  strncpy(e.who, o == ORIGIN_TAILNET ? "tailnet" : "public", sizeof(e.who) - 1); e.who[sizeof(e.who) - 1] = 0;
  strncpy(e.action, action, sizeof(e.action) - 1);                                e.action[sizeof(e.action) - 1] = 0;
  strncpy(e.detail, detail ? detail : "", sizeof(e.detail) - 1);                   e.detail[sizeof(e.detail) - 1] = 0;
  logHead = (logHead + 1) % LOG_N;
  if (logCount < LOG_N) logCount++;
  Serial.printf("[audit] %s %s %s\n", e.who, e.action, e.detail);
}

String access_log_json() {
  String s = "[";
  for (int i = 0; i < logCount; i++) {
    int idx = (logHead - 1 - i + LOG_N) % LOG_N;
    LogEntry& e = logs[idx];
    if (i) s += ",";
    s += "{\"t\":"; s += String(e.ms);
    s += ",\"who\":\""; s += e.who;
    s += "\",\"action\":\""; s += e.action;
    s += "\",\"detail\":\""; s += e.detail; s += "\"}";
  }
  s += "]";
  return s;
}
