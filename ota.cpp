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
  // Require a *completed* write (final chunk seen + Update.end() succeeded), not merely
  // "no error recorded". On a lossy link the upload stream can reset before the final
  // chunk, so Update.end(true) never runs and the boot partition is never switched —
  // yet hasError() stays false. Without isFinished() we'd falsely report success and the
  // unit would silently stay on the old image (observed on the high-latency unit .56).
  bool ok = Update.isFinished() && !Update.hasError();
  String body;
  if (ok) {
    body = "{\"ok\":true,\"reboot\":true}";
  } else {
    String e = Update.hasError() ? Update.errorString() : String("incomplete upload (stream reset)");
    Serial.printf("[ota] FAILED: %s\n", e.c_str());
    body = String("{\"err\":\"update failed: ") + e + "\"}";
  }
  AsyncWebServerResponse* res = req->beginResponse(ok ? 200 : 500, "application/json", body);
  res->addHeader("Connection", "close");
  req->send(res);
  if (ok) { access_log(ORIGIN_TAILNET, "ota", "flashed; rebooting"); pendingReboot = true; }
}
