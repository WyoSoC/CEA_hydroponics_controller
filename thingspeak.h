#pragma once
#include "sensors.h"

// Periodic upload of readings to ThingSpeak (field1=pH, field2=EC mS/cm, field3=temp).
// Durable cloud history, independent of the device (survives reboots/OTA). All HTTP
// runs from the main loop. Config (Write API Key, enable, interval) persisted in NVS.
void        ts_begin();
void        ts_tick(const SensorState& s);
void        ts_set(const String& key, bool on, int intervalS);
void        ts_request_test();
const char* ts_key();
bool        ts_enabled();
int         ts_interval();
