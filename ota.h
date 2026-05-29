#pragma once
#include <ESPAsyncWebServer.h>

// Over-the-air firmware update over HTTP, TAILNET ONLY (firmware injection is the
// most privileged action — never allowed from public, even when unlocked).
// Wired into the web server as: server.on("/api/ota", HTTP_POST, ota_on_request, ota_on_upload)
void ota_on_request(AsyncWebServerRequest* req);
void ota_on_upload(AsyncWebServerRequest* req, const String& filename,
                   size_t index, uint8_t* data, size_t len, bool final);

bool web_ota_pending_reboot();   // loop() reboots when this is true
