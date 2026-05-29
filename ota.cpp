#include "ota.h"
#include "access.h"
#include <Arduino.h>
#include <Update.h>

static bool authorized   = false;   // set per-upload at the first chunk
static bool pendingReboot = false;

bool web_ota_pending_reboot() { return pendingReboot; }

// Streaming upload handler: authorize on the first chunk (tailnet only), then
// write chunks straight to the inactive OTA partition.
void ota_on_upload(AsyncWebServerRequest* req, const String& filename,
                   size_t index, uint8_t* data, size_t len, bool final) {
  if (index == 0) {
    authorized = (access_origin(req) == ORIGIN_TAILNET);
    if (!authorized) { Serial.println(F("[ota] DENIED (not tailnet)")); return; }
    Serial.printf("[ota] start: %s\n", filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  }
  if (!authorized) return;
  if (len && Update.write(data, len) != len) Update.printError(Serial);
  if (final) {
    if (Update.end(true)) Serial.printf("[ota] received %u bytes OK\n", (unsigned)(index + len));
    else Update.printError(Serial);
  }
}

// Final response (runs after the upload completes).
void ota_on_request(AsyncWebServerRequest* req) {
  if (!authorized) { req->send(403, "application/json", "{\"err\":\"tailnet only\"}"); return; }
  bool ok = !Update.hasError();
  String body;
  if (ok) {
    body = "{\"ok\":true,\"reboot\":true}";
  } else {
    String e = Update.errorString();              // e.g. "Not Enough Space", "No partition"
    Serial.printf("[ota] FAILED: %s\n", e.c_str());
    body = String("{\"err\":\"update failed: ") + e + "\"}";
  }
  AsyncWebServerResponse* res = req->beginResponse(ok ? 200 : 500, "application/json", body);
  res->addHeader("Connection", "close");
  req->send(res);
  if (ok) { access_log(ORIGIN_TAILNET, "ota", "flashed; rebooting"); pendingReboot = true; }
}
