#pragma once
#include <Arduino.h>

// Per-unit identity (name + mDNS hostname), persisted in NVS so ONE firmware image
// self-identifies across all units and the name survives OTA. When unset, defaults
// to a MAC-derived unique value (hydro-XXXXXX) so units don't collide out of the box.
void        identity_begin();
const char* identity_name();
const char* identity_host();
void        identity_set(const String& name, const String& host);  // persists; sanitizes host
bool        identity_consume_reboot();   // true once after identity_set (host change needs reboot)
