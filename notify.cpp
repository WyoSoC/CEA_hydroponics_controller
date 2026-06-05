#include "notify.h"
#include "config.h"
#include "settings.h"
#include "identity.h"
#include "net.h"
#include "secrets.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static Preferences   np;
static String        g_dest;       // user entry: an EMAIL address OR a webhook URL
static bool          g_on = false;
static String        prevAlarms = "";
static unsigned long lastProcessed = 0;
static bool          testReq = false;
static unsigned long alarmStartMs = 0;   // when the current alarm episode began
static String        alarmStartStr = ""; // wall-clock string at episode start

void notify_begin() {
  np.begin("notify", false);
  g_dest = np.getString("url", "");
  g_on   = np.getInt("on", 0) != 0;
}
const char* notify_url()     { return g_dest.c_str(); }
bool        notify_enabled() { return g_on; }
void        notify_set(const String& dest, bool on) {
  g_dest = dest; g_on = on;
  np.putString("url", dest); np.putInt("on", on ? 1 : 0);
}
void notify_request_test() { testReq = true; }

static bool isEmail(const String& s) { return s.indexOf('@') > 0 && !s.startsWith("http"); }

static String computeAlarms(const SensorState& s, const Settings& set) {
  String a;
  float ec_mS = s.ec / 1000.0f;
  if (s.phValid && s.ph  < set.ph_lo) { if (a.length()) a += ","; a += "pH LOW";  }
  if (s.phValid && s.ph  > set.ph_hi) { if (a.length()) a += ","; a += "pH HIGH"; }
  if (s.ecValid && ec_mS < set.ec_lo) { if (a.length()) a += ","; a += "EC LOW";  }
  if (s.ecValid && ec_mS > set.ec_hi) { if (a.length()) a += ","; a += "EC HIGH"; }
  return a;
}

static String fmtDuration(unsigned long ms) {
  unsigned long s = ms / 1000, m = s / 60, h = m / 60;
  s %= 60; m %= 60;
  char b[24];
  if (h) snprintf(b, sizeof(b), "%luh %lum", h, m);
  else if (m) snprintf(b, sizeof(b), "%lum %lus", m, s);
  else snprintf(b, sizeof(b), "%lus", s);
  return String(b);
}

static bool httpPost(const String& url, const String& body) {
  if (!net_connected() || url.length() < 8) return false;
  HTTPClient http;
  http.setConnectTimeout(5000); http.setTimeout(8000);
  bool ok = false;
  if (url.startsWith("https")) {
    WiFiClientSecure cs; cs.setInsecure();
    if (http.begin(cs, url)) {
      http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // Apps Script 302-redirects on POST
      http.addHeader("Content-Type", "application/json");
      int c = http.POST(body); ok = (c >= 200 && c < 300); http.end();
    }
  } else {
    WiFiClient cl;
    if (http.begin(cl, url)) {
      http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
      http.addHeader("Content-Type", "application/json");
      int c = http.POST(body); ok = (c >= 200 && c < 300); http.end();
    }
  }
  Serial.printf("[notify] POST -> %s\n", ok ? "ok" : "FAIL");
  return ok;
}

static String jescape(const String& in) {   // minimal: keep JSON valid
  String o; for (size_t i = 0; i < in.length(); i++) { char c = in[i]; if (c != '"' && c != '\\') o += c; } return o;
}

// `timing` is a human phrase already formatted (e.g. "triggered 2026-06-05 14:23:01").
static bool sendAlert(const char* status, const String& alarms, const SensorState& s, const String& timing) {
  if (g_dest.length() < 3) return false;
  bool email = isEmail(g_dest);
  String target = email ? String(EMAIL_RELAY_URL) : g_dest;
  String to     = email ? g_dest : String("");

  char head[180];
  snprintf(head, sizeof(head), "%s %s: %s (pH %.2f, EC %.2f mS, T %.1f C)",
           identity_name(), status, alarms.length() ? alarms.c_str() : "-",
           s.ph, s.ec / 1000.0, s.tempC);
  String msg = String(head) + " | " + timing;

  String b = "{";
  b += "\"to\":\"";     b += jescape(to);            b += "\",";
  b += "\"unit\":\"";   b += jescape(identity_name()); b += "\",";
  b += "\"status\":\""; b += status;                 b += "\",";
  b += "\"alarms\":\""; b += jescape(alarms);        b += "\",";
  b += "\"time\":\"";   b += jescape(net_time_str()); b += "\",";
  b += "\"msg\":\"";    b += jescape(msg);           b += "\",";
  b += "\"ph\":";       b += s.phValid   ? String(s.ph, 2)          : String("null"); b += ",";
  b += "\"ec\":";       b += s.ecValid   ? String(s.ec / 1000.0, 2) : String("null"); b += ",";
  b += "\"temp\":";     b += s.tempValid ? String(s.tempC, 1)       : String("null");
  b += "}";
  return httpPost(target, b);
}

void notify_tick(const SensorState& s) {
  if (testReq) { testReq = false; sendAlert("TEST", String("test"), s, "sent " + net_time_str()); }

  if (!g_on) return;
  if (s.lastUpdateMs == 0 || s.lastUpdateMs == lastProcessed) return;   // act once per fresh reading
  lastProcessed = s.lastUpdateMs;

  String cur = computeAlarms(s, settings_get());
  if (cur == prevAlarms) return;                                        // ONLY on a state change

  unsigned long now = millis();
  if (cur.length()) {                                                   // entering / changing alarm
    if (prevAlarms.length() == 0) { alarmStartMs = now; alarmStartStr = net_time_str(); }
    sendAlert("ALARM", cur, s, "triggered " + net_time_str());
  } else {                                                              // back to normal
    String dur = alarmStartMs ? fmtDuration(now - alarmStartMs) : String("?");
    sendAlert("CLEAR", prevAlarms, s, "cleared " + net_time_str() + ", lasted " + dur);
    alarmStartMs = 0; alarmStartStr = "";
  }
  prevAlarms = cur;
}
