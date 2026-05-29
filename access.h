#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Access control: request-origin classification + the runtime "lock" that gates
// public (Funnel) control, plus a small in-RAM audit log.
//
// Lock model:
//   - tailnet origin  -> always allowed to control AND to toggle the lock
//   - public origin   -> may control ONLY while unlocked; can never toggle
//   - default LOCKED, locked on every boot, auto-relocks when the unlock expires

enum Origin { ORIGIN_TAILNET, ORIGIN_PUBLIC };

void access_begin();
void access_tick();                   // call in loop(): handles unlock expiry
bool access_consume_lock_changed();   // true once after any lock-state change

Origin access_origin(AsyncWebServerRequest* req);
bool    access_secret_ok(AsyncWebServerRequest* req);

bool     access_locked();
uint32_t access_unlock_remaining_s();
void     access_lock();
void     access_unlock(uint32_t minutes);

bool access_can_control(Origin o);      // tailnet always; public only when unlocked
bool access_can_toggle_lock(Origin o);  // tailnet only

void   access_log(Origin o, const char* action, const char* detail);
String access_log_json();               // newest-first JSON array
