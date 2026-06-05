#pragma once
#include "sensors.h"

// Alarm notifier: on an alarm transition, HTTP-POSTs a JSON alert to a configurable
// webhook URL (point it at a Google Apps Script for email, or ntfy/Slack/Discord/etc).
// Edge-triggered (ALARM on enter, CLEAR on exit), re-sends every NOTIFY_RENOTIFY_MS
// while active. All HTTP runs from the main loop (never the async web task).
void        notify_begin();
void        notify_tick(const SensorState& s);
void        notify_set(const String& url, bool on);   // persists to NVS
void        notify_request_test();                     // queue a one-off test POST
const char* notify_url();
bool        notify_enabled();
