#pragma once
#include "sensors.h"

// Alarm notifier: on an alarm transition, HTTP-POSTs a JSON alert to a configurable
// webhook URL (point it at a Google Apps Script for email, or ntfy/Slack/Discord/etc).
// Edge-triggered: one ALARM on enter, one CLEAR on return-to-normal (no re-sends while
// active). The blocking HTTP runs on the dedicated nettask (never the async web task).
void        notify_begin();
void        notify_tick(const SensorState& s);
void        notify_set(const String& url, bool on);   // persists to NVS
void        notify_request_test();                     // queue a one-off test POST
const char* notify_url();
bool        notify_enabled();
