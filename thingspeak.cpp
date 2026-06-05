#include "thingspeak.h"
#include "config.h"
#include "net.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>

static Preferences   tp;
static String        g_key;
static bool          g_on = false;
static int           g_int = TS_DEFAULT_INTERVAL_S;
static unsigned long lastPost = 0;
static bool          testReq = false;

void ts_begin() {
  tp.begin("ts", false);
  g_key = tp.getString("key", "");
  g_on  = tp.getInt("on", 0) != 0;
  g_int = tp.getInt("int", TS_DEFAULT_INTERVAL_S);
  if (g_int < TS_MIN_INTERVAL_S) g_int = TS_MIN_INTERVAL_S;
}
const char* ts_key()      { return g_key.c_str(); }
bool        ts_enabled()  { return g_on; }
int         ts_interval() { return g_int; }
void ts_set(const String& key, bool on, int intervalS) {
  g_key = key; g_on = on;
  g_int = (intervalS < TS_MIN_INTERVAL_S) ? TS_MIN_INTERVAL_S : intervalS;
  tp.putString("key", g_key); tp.putInt("on", on ? 1 : 0); tp.putInt("int", g_int);
}
void ts_request_test() { testReq = true; }

static bool ts_post(const SensorState& s) {
  if (!net_connected() || g_key.length() < 4) return false;
  String url = "http://api.thingspeak.com/update?api_key=" + g_key;
  if (s.phValid)   url += "&field1=" + String(s.ph, 2);
  if (s.ecValid)   url += "&field2=" + String(s.ec / 1000.0, 3);   // mS/cm
  if (s.tempValid) url += "&field3=" + String(s.tempC, 1);

  HTTPClient http; WiFiClient cl;
  http.setConnectTimeout(5000); http.setTimeout(8000);
  bool ok = false;
  if (http.begin(cl, url)) {
    int code = http.GET();
    String body = http.getString();        // ThingSpeak returns the new entry ID (>0) on success, 0 on fail/rate-limit
    ok = (code == 200 && body.toInt() > 0);
    http.end();
  }
  Serial.printf("[ts] update -> %s\n", ok ? "ok" : "FAIL");
  return ok;
}

void ts_tick(const SensorState& s) {
  if (testReq) { testReq = false; ts_post(s); }
  if (!g_on) return;
  unsigned long now = millis();
  if (lastPost != 0 && now - lastPost < (unsigned long)g_int * 1000UL) return;
  lastPost = now;
  ts_post(s);
}
