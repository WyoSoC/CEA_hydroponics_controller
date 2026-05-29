#pragma once
// Copy this file to secrets.h and fill in deployment-specific values.
// Do not commit secrets.h.

#define WIFI_SSID  "your-wifi-ssid"
#define WIFI_PASS  "your-wifi-password"

// Shared secret injected by the reverse proxy as X-Proxy-Secret.
// Use a long random value and keep it synchronized with proxy config.
#define PROXY_SECRET "replace-with-long-random-secret"
