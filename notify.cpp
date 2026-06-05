#include "notify.h"
#include "config.h"
#include "settings.h"
#include "identity.h"
#include "net.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static Preferences   np;
static String        g_url;
static bool          g_on = false;
static String        prevAlarms = "";
static unsigned long lastSent = 0;
static unsigned long lastProcessed = 0;
static bool          testReq = false;

void notify_begin() {
  np.begin("notify", false);
  g_url = np.getString("url", "");
  g_on  = np.getInt("on", 0) != 0;
}
const char* notify_url()     { return g_url.c_str(); }
bool        notify_enabled() { return g_on; }
void        notify_set(const String& url, bool on) {
  g_url = url; g_on = on;
  np.putString("url", url); np.putInt("on", on ? 1 : 0);
}
void notify_request_test() { testReq = true; }

static String computeAlarms(const SensorState& s, const Settings& set) {
  String a;
  float ec_mS = s.ec / 1000.0f;
  if (s.phValid && s.ph  < set.ph_lo) { if (a.length()) a += ","; a += "pH LOW";  }
  if (s.phValid && s.ph  > set.ph_hi) { if (a.length()) a += ","; a += "pH HIGH"; }
  if (s.ecValid && ec_mS < set.ec_lo) { if (a.length()) a += ","; a += "EC LOW";  }
  if (s.ecValid && ec_mS > set.ec_hi) { if (a.length()) a += ","; a += "EC HIGH"; }
  return a;
}

static bool postJson(const String& body) {
  if (!net_connected() || g_url.length() < 8) return false;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(8000);
  bool ok = false;
  if (g_url.startsWith("https")) {
    WiFiClientSecure cs; cs.setInsecure();                 // outbound alert; no cert pinning
    if (http.begin(cs, g_url)) { http.addHeader("Content-Type", "application/json"); int c = http.POST(body); ok = (c >= 200 && c < 300); http.end(); }
  } else {
    WiFiClient cl;
    if (http.begin(cl, g_url)) { http.addHeader("Content-Type", "application/json"); int c = http.POST(body); ok = (c >= 200 && c < 300); http.end(); }
  }
  Serial.printf("[notify] POST -> %s\n", ok ? "ok" : "FAIL");
  return ok;
}

static String buildBody(const char* status, const String& alarms, const SensorState& s) {
  char msg[170];
  snprintf(msg, sizeof(msg), "%s %s: %s (pH %.2f, EC %.2f mS, T %.1f C)",
           identity_name(), status, alarms.length() ? alarms.c_str() : "-",
           s.ph, s.ec / 1000.0, s.tempC);
  String b = "{";
  b += "\"unit\":\"";   b += identity_name();     b += "\",";
  b += "\"status\":\""; b += status;              b += "\",";
  b += "\"alarms\":\""; b += alarms;              b += "\",";
  b += "\"msg\":\"";    b += msg;                 b += "\",";
  b += "\"ph\":";       b += s.phValid   ? String(s.ph, 2)        : String("null"); b += ",";
  b += "\"ec\":";       b += s.ecValid   ? String(s.ec / 1000.0, 2): String("null"); b += ",";
  b += "\"temp\":";     b += s.tempValid ? String(s.tempC, 1)     : String("null");
  b += "}";
  return b;
}

void notify_tick(const SensorState& s) {
  if (testReq) { testReq = false; postJson(buildBody("TEST", String("test"), s)); }

  if (!g_on) return;
  if (s.lastUpdateMs == 0 || s.lastUpdateMs == lastProcessed) return;   // only on a fresh reading
  lastProcessed = s.lastUpdateMs;

  String cur = computeAlarms(s, settings_get());
  unsigned long now = millis();

  if (cur != prevAlarms) {                       // entered/changed/cleared
    if (cur.length())            postJson(buildBody("ALARM", cur, s));
    else if (prevAlarms.length())postJson(buildBody("CLEAR", prevAlarms, s));
    prevAlarms = cur; lastSent = now;
  } else if (cur.length() && now - lastSent >= NOTIFY_RENOTIFY_MS) {    // reminder while active
    postJson(buildBody("ALARM", cur, s));
    lastSent = now;
  }
}
