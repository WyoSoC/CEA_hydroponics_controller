#include "webserver.h"
#include "index_html.h"
#include "config.h"
#include "devices.h"
#include "sensors.h"
#include "control.h"
#include "commands.h"
#include "access.h"
#include "ota.h"
#include "settings.h"
#include "identity.h"
#include "history.h"
#include "notify.h"
#include "tune.h"
#include "thingspeak.h"
#include "secrets.h"
#include <Arduino.h>
#include <math.h>
#include <memory>
#include <string.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static const char* FW_VERSION = __DATE__ " " __TIME__;   // build stamp, for fleet verification

// ---- helpers ---------------------------------------------------------------

// read a query/body parameter as String ("" if absent)
static String qp(AsyncWebServerRequest* r, const char* k) {
  if (r->hasParam(k))       return r->getParam(k)->value();
  if (r->hasParam(k, true)) return r->getParam(k, true)->value();
  return String();
}

static void sendJson(AsyncWebServerRequest* r, int code, const String& body) {
  r->send(code, "application/json", body);
}

// Build the live-state JSON (NaN-safe): readings (EC in uS), lock, alarms, settings.
static void buildJson(char* out, size_t n) {
  SensorState s = sensors_snapshot();
  const Settings& set = settings_get();
  float ph   = isnan(s.ph)    ? 0.0f : s.ph;
  float ec   = isnan(s.ec)    ? 0.0f : s.ec;       // uS/cm (UI divides by 1000 for mS)
  float temp = isnan(s.tempC) ? 0.0f : s.tempC;
  float ec_mS = ec / 1000.0f;
  unsigned long age = (s.lastUpdateMs == 0) ? 0UL : (millis() - s.lastUpdateMs);

  bool phLo = s.phValid && s.ph  < set.ph_lo;
  bool phHi = s.phValid && s.ph  > set.ph_hi;
  bool ecLo = s.ecValid && ec_mS < set.ec_lo;
  bool ecHi = s.ecValid && ec_mS > set.ec_hi;

  snprintf(out, n,
    "{\"name\":\"%s\",\"ph\":%.2f,\"phOk\":%s,\"ec\":%.0f,\"ecOk\":%s,"
    "\"temp\":%.1f,\"tempOk\":%s,\"ageMs\":%lu,\"auto\":%s,\"queue\":%u,"
    "\"locked\":%s,\"lockRemain\":%lu,\"uptime\":%lu,"
    "\"phLo\":%s,\"phHi\":%s,\"ecLo\":%s,\"ecHi\":%s,"
    "\"phSp\":%.2f,\"phAlo\":%.2f,\"phAhi\":%.2f,\"phKp\":%.2f,\"phKi\":%.3f,\"phMin\":%u,"
    "\"ecSp\":%.2f,\"ecAlo\":%.2f,\"ecAhi\":%.2f,\"ecKp\":%.2f,\"ecKi\":%.3f,\"ecMin\":%u,"
    "\"iPh\":%.2f,\"iEc\":%.2f,\"phF\":%d,\"ecF\":%d,\"tF\":%d}",
    identity_name(),
    ph,   s.phValid   ? "true" : "false",
    ec,   s.ecValid   ? "true" : "false",
    temp, s.tempValid ? "true" : "false",
    age,  control_auto_enabled() ? "true" : "false",
    (unsigned)commands_pending(),
    access_locked() ? "true" : "false",
    (unsigned long)access_unlock_remaining_s(),
    (unsigned long)(millis() / 1000),
    phLo ? "true" : "false", phHi ? "true" : "false",
    ecLo ? "true" : "false", ecHi ? "true" : "false",
    set.ph_sp, set.ph_lo, set.ph_hi, set.ph_kp, set.ph_ki, (unsigned)set.ph_min,
    set.ec_sp, set.ec_lo, set.ec_hi, set.ec_kp, set.ec_ki, (unsigned)set.ec_min,
    control_integral_ph(), control_integral_ec(),
    sensors_fail_ph(), sensors_fail_ec(), sensors_fail_temp());
}

// ---- websocket -------------------------------------------------------------

static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    char buf[640];
    buildJson(buf, sizeof(buf));
    client->text(buf);
  }
  // Commands arrive via HTTP POST (so headers are readable for auth), not WS.
}

// Stream a command result line to all browsers as an event.
static void cmdReporter(const char* msg) {
  String j = "{\"event\":\"";
  for (const char* p = msg; *p; ++p) { if (*p != '"' && *p != '\\') j += *p; }  // keep JSON valid
  j += "\"}";
  ws.textAll(j);
}

// ---- HTTP endpoints --------------------------------------------------------

static void registerRoutes() {
  // page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* r = req->beginResponse_P(200, "text/html", INDEX_HTML);
    r->addHeader("Cache-Control", "no-store");   // always serve fresh page after an OTA
    req->send(r);
  });

  // who is this caller (origin), for the UI to decide what to show
  server.on("/api/whoami", HTTP_GET, [](AsyncWebServerRequest* req) {
    Origin o = access_origin(req);
    String j = String("{\"origin\":\"") + (o == ORIGIN_TAILNET ? "tailnet" : "public") +
               "\",\"secretOk\":" + (access_secret_ok(req) ? "true" : "false") +
               ",\"fw\":\"" + FW_VERSION + "\",\"name\":\"" + identity_name() + "\"}";
    sendJson(req, 200, j);
  });

  // lock / unlock — tailnet only
  server.on("/api/lock", HTTP_POST, [](AsyncWebServerRequest* req) {
    Origin o = access_origin(req);
    if (!access_can_toggle_lock(o)) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    access_lock(); access_log(o, "lock", "");
    sendJson(req, 200, "{\"ok\":true,\"locked\":true}");
  });

  server.on("/api/unlock", HTTP_POST, [](AsyncWebServerRequest* req) {
    Origin o = access_origin(req);
    if (!access_can_toggle_lock(o)) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    uint32_t m = qp(req, "min").length() ? (uint32_t)qp(req, "min").toInt() : DEFAULT_UNLOCK_MIN;
    if (m < 1) m = 1; if (m > MAX_UNLOCK_MIN) m = MAX_UNLOCK_MIN;
    access_unlock(m);
    char d[16]; snprintf(d, sizeof(d), "%u min", (unsigned)m); access_log(o, "unlock", d);
    sendJson(req, 200, String("{\"ok\":true,\"locked\":false,\"min\":") + m + "}");
  });

  // dispense — control (tailnet always; public only when unlocked)
  server.on("/api/pump", HTTP_POST, [](AsyncWebServerRequest* req) {
    Origin o = access_origin(req);
    if (!access_can_control(o)) { sendJson(req, 403, "{\"err\":\"locked\"}"); return; }
    int   id = qp(req, "id").toInt();
    float ml = qp(req, "ml").toFloat();
    if (id < 1 || id > 3)   { sendJson(req, 400, "{\"err\":\"id must be 1..3\"}"); return; }
    if (ml >  MANUAL_MAX_DOSE_ML) ml =  MANUAL_MAX_DOSE_ML;   // clamp
    if (ml < -MANUAL_MAX_DOSE_ML) ml = -MANUAL_MAX_DOSE_ML;
    Command c; c.type = CMD_DISPENSE; c.target = (uint8_t)(id - 1); c.value = ml; c.raw[0] = 0;
    bool ok = commands_enqueue(c);
    char d[32]; snprintf(d, sizeof(d), "pump %d %.2f mL", id, ml); access_log(o, "dispense", d);
    sendJson(req, ok ? 200 : 503, ok ? "{\"ok\":true}" : "{\"err\":\"queue full\"}");
  });

  // stop pump(s) — control
  server.on("/api/pump/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
    Origin o = access_origin(req);
    if (!access_can_control(o)) { sendJson(req, 403, "{\"err\":\"locked\"}"); return; }
    String idStr = qp(req, "id");
    if (idStr.length() == 0) {
      for (uint8_t i = 0; i < 3; i++) { Command c; c.type = CMD_PUMP_STOP; c.target = i; c.value = 0; c.raw[0] = 0; commands_enqueue(c); }
      access_log(o, "stop", "all");
    } else {
      int id = idStr.toInt();
      if (id < 1 || id > 3) { sendJson(req, 400, "{\"err\":\"id must be 1..3\"}"); return; }
      Command c; c.type = CMD_PUMP_STOP; c.target = (uint8_t)(id - 1); c.value = 0; c.raw[0] = 0;
      commands_enqueue(c);
      char d[16]; snprintf(d, sizeof(d), "pump %d", id); access_log(o, "stop", d);
    }
    sendJson(req, 200, "{\"ok\":true}");
  });

  // raw calibration command to a device — control
  server.on("/api/cal", HTTP_POST, [](AsyncWebServerRequest* req) {
    Origin o = access_origin(req);
    if (!access_can_control(o)) { sendJson(req, 403, "{\"err\":\"locked\"}"); return; }
    String dev = qp(req, "dev");
    String cmd = qp(req, "cmd");
    int idx = device_index_from_token(dev);
    if (idx < 0)                                  { sendJson(req, 400, "{\"err\":\"dev=ph|ec|rtd|p1|p2|p3\"}"); return; }
    if (cmd.length() == 0 || cmd.length() >= 24)  { sendJson(req, 400, "{\"err\":\"bad cmd\"}"); return; }
    Command c; c.type = CMD_RAW; c.target = (uint8_t)idx; c.value = 0;
    cmd.toCharArray(c.raw, sizeof(c.raw));
    bool ok = commands_enqueue(c);
    access_log(o, "cal", (dev + " " + cmd).c_str());
    sendJson(req, ok ? 200 : 503, ok ? "{\"ok\":true}" : "{\"err\":\"queue full\"}");
  });

  // autonomous dosing on/off — control
  server.on("/api/auto", HTTP_POST, [](AsyncWebServerRequest* req) {
    Origin o = access_origin(req);
    if (!access_can_control(o)) { sendJson(req, 403, "{\"err\":\"locked\"}"); return; }
    bool on = qp(req, "on") == "1" || qp(req, "on") == "true";
    Command c; c.type = CMD_SET_AUTO; c.target = 0; c.value = on ? 1 : 0; c.raw[0] = 0;
    commands_enqueue(c);
    access_log(o, "auto", on ? "on" : "off");
    sendJson(req, 200, "{\"ok\":true}");
  });

  // audit log — tailnet only
  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    sendJson(req, 200, access_log_json());
  });

  // control settings — read any origin; write is control-gated
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
    sendJson(req, 200, settings_json());
  });
  server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* req) {
    Origin o = access_origin(req);
    if (!access_can_control(o)) { sendJson(req, 403, "{\"err\":\"locked\"}"); return; }
    static const char* keys[] = {
      "phSp","phAlo","phAhi","phKp","phKi","phMin",
      "ecSp","ecAlo","ecAhi","ecKp","ecKi","ecMin"
    };
    int n = 0;
    for (const char* k : keys) { String v = qp(req, k); if (v.length() && settings_apply(k, v.toFloat())) n++; }
    char d[16]; snprintf(d, sizeof(d), "%d fields", n); access_log(o, "settings", d);
    web_broadcast();
    sendJson(req, 200, settings_json());
  });

  // per-unit identity — read any origin; rename is tailnet only (reboots to apply)
  server.on("/api/identity", HTTP_GET, [](AsyncWebServerRequest* req) {
    String j = String("{\"name\":\"") + identity_name() + "\",\"host\":\"" + identity_host() + "\"}";
    sendJson(req, 200, j);
  });
  server.on("/api/identity", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    String name = qp(req, "name"), host = qp(req, "host");
    if (name.length() == 0 && host.length() == 0) { sendJson(req, 400, "{\"err\":\"name or host required\"}"); return; }
    identity_set(name, host);
    access_log(ORIGIN_TAILNET, "identity", (name + " " + host).c_str());
    String j = String("{\"ok\":true,\"reboot\":true,\"name\":\"") + identity_name() +
               "\",\"host\":\"" + identity_host() + "\"}";   // return the sanitized applied values
    sendJson(req, 200, j);
  });

  // history — downsampled JSON for charting (read; any origin)
  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* req) {
    size_t n = qp(req, "n").length() ? (size_t)qp(req, "n").toInt() : 600;
    if (n < 10) n = 10; if (n > 2000) n = 2000;
    sendJson(req, 200, history_json(n));
  });

  // logging interval (5/10/30 s) — tailnet only (changing clears history)
  server.on("/api/loginterval", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    int s = qp(req, "s").toInt();
    if (!history_set_interval(s)) { sendJson(req, 400, "{\"err\":\"s must be 1..3600\"}"); return; }
    access_log(ORIGIN_TAILNET, "loginterval", String(s).c_str());
    sendJson(req, 200, String("{\"ok\":true,\"interval\":") + s + "}");
  });

  // clear the logged history — tailnet only
  server.on("/api/history/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    history_clear(); access_log(ORIGIN_TAILNET, "history", "cleared");
    sendJson(req, 200, "{\"ok\":true}");
  });

  // history CSV export — chunked so we never build a multi-MB String in RAM
  server.on("/api/history.csv", HTTP_GET, [](AsyncWebServerRequest* req) {
    auto cursor = std::make_shared<size_t>(0);
    size_t total = history_count();
    AsyncWebServerResponse* res = req->beginChunkedResponse("text/csv",
      [cursor, total](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
        size_t w = 0;
        if (index == 0) {
          const char* h = "sec_ago,ph,ec_mS,temp\n"; size_t hl = strlen(h);
          if (hl > maxLen) return 0;
          memcpy(buffer, h, hl); w += hl;
        }
        char line[48];
        while (*cursor < total) {
          size_t l = history_csv_line(*cursor, line, sizeof(line));
          if (w + l > maxLen) break;
          memcpy(buffer + w, line, l); w += l; (*cursor)++;
        }
        return w;   // 0 -> response complete
      });
    res->addHeader("Content-Disposition", "attachment; filename=hydro_history.csv");
    req->send(res);
  });

  // alarm notifier config — tailnet only
  server.on("/api/notify", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    sendJson(req, 200, String("{\"on\":") + (notify_enabled() ? "true" : "false") +
                       ",\"url\":\"" + notify_url() + "\"}");
  });
  server.on("/api/notify", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    bool on = (qp(req, "on") == "1" || qp(req, "on") == "true");
    notify_set(qp(req, "url"), on);
    access_log(ORIGIN_TAILNET, "notify", on ? "on" : "off");
    sendJson(req, 200, "{\"ok\":true}");
  });
  server.on("/api/notify/test", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    notify_request_test();          // actual POST happens in the main loop
    sendJson(req, 200, "{\"ok\":true,\"queued\":true}");
  });

  // auto-tune — status readable; start/apply/abort are control-gated
  server.on("/api/tune", HTTP_GET, [](AsyncWebServerRequest* req) {
    sendJson(req, 200, tune_status_json());
  });
  server.on("/api/tune/start", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!access_can_control(access_origin(req))) { sendJson(req, 403, "{\"err\":\"locked\"}"); return; }
    String ch = qp(req, "ch");
    char c = (ch == "ph") ? 'p' : (ch == "ec") ? 'e' : 0;
    bool ok = (c != 0) && tune_start(c);
    access_log(access_origin(req), "tune", ch.c_str());
    sendJson(req, ok ? 200 : 409, ok ? "{\"ok\":true}" : "{\"err\":\"cannot start (busy/invalid/no reading)\"}");
  });
  server.on("/api/tune/apply", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!access_can_control(access_origin(req))) { sendJson(req, 403, "{\"err\":\"locked\"}"); return; }
    tune_apply(); access_log(access_origin(req), "tune", "apply");
    sendJson(req, 200, "{\"ok\":true}");
  });
  server.on("/api/tune/abort", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!access_can_control(access_origin(req))) { sendJson(req, 403, "{\"err\":\"locked\"}"); return; }
    tune_abort();
    sendJson(req, 200, "{\"ok\":true}");
  });

  // ThingSpeak cloud upload config — tailnet only
  server.on("/api/thingspeak", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    sendJson(req, 200, String("{\"on\":") + (ts_enabled() ? "true" : "false") +
                       ",\"key\":\"" + ts_key() + "\",\"interval\":" + ts_interval() + "}");
  });
  server.on("/api/thingspeak", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    bool on = (qp(req, "on") == "1" || qp(req, "on") == "true");
    int iv = qp(req, "interval").length() ? qp(req, "interval").toInt() : TS_DEFAULT_INTERVAL_S;
    ts_set(qp(req, "key"), on, iv);
    access_log(ORIGIN_TAILNET, "thingspeak", on ? "on" : "off");
    sendJson(req, 200, "{\"ok\":true}");
  });
  server.on("/api/thingspeak/test", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (access_origin(req) != ORIGIN_TAILNET) { sendJson(req, 403, "{\"err\":\"tailnet only\"}"); return; }
    ts_request_test();
    sendJson(req, 200, "{\"ok\":true,\"queued\":true}");
  });

  // OTA firmware upload — tailnet only (see ota.cpp)
  server.on("/api/ota", HTTP_POST, ota_on_request, ota_on_upload);

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "not found");
  });
}

// ---- public API ------------------------------------------------------------

void web_begin() {
  commands_set_reporter(cmdReporter);
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  registerRoutes();
  server.begin();
  Serial.println(F("[web] HTTP on :80, WebSocket at /ws"));
}

void web_tick() { ws.cleanupClients(); }

void web_broadcast() {
  if (ws.count() == 0) return;
  char buf[768];
  buildJson(buf, sizeof(buf));
  ws.textAll(buf);
}
