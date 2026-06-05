#include "net.h"
#include "secrets.h"
#include "identity.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>

static unsigned long lastRetry = 0;

void net_begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(identity_host());
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[net] connecting to SSID '%s' as '%s' ...\n", WIFI_SSID, identity_host());
  lastRetry = millis();
}

bool net_connected() { return WiFi.status() == WL_CONNECTED; }

void net_tick() {
  static bool was = false;
  bool now = net_connected();

  if (now && !was) {
    Serial.printf("[net] connected. IP=%s  ->  http://%s.local/\n",
                  WiFi.localIP().toString().c_str(), identity_host());
    if (MDNS.begin(identity_host())) MDNS.addService("http", "tcp", 80);
    configTzTime(TZ_INFO, NTP_SERVER1, NTP_SERVER2);   // start NTP (local time, auto-DST)
    was = true;
  } else if (!now && was) {
    Serial.println(F("[net] Wi-Fi lost; auto-reconnecting"));
    was = false;
  }

  if (!now && millis() - lastRetry > 10000) {   // nudge a reconnect every 10 s
    lastRetry = millis();
    WiFi.reconnect();
  }
}

bool net_time_valid() { struct tm t; return getLocalTime(&t, 5); }   // 5 ms, non-blocking

String net_time_str() {
  struct tm t;
  if (getLocalTime(&t, 50)) {
    char b[24]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &t);
    return String(b);
  }
  return String("uptime ") + String((unsigned long)(millis() / 1000)) + "s";
}
