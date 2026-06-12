#pragma once
#include "sensors.h"

// Local data log: a fixed-cadence ring buffer (pH/EC/temp) in PSRAM. Timestamps are
// implicit (LOG_INTERVAL_S apart). Volatile — cleared on reboot/OTA/power loss.
void   history_begin();
void   history_tick(const SensorState& s);   // samples once per LOG_INTERVAL_S
size_t history_count();
size_t history_capacity();
bool   history_in_psram();
int    history_interval_s();
bool   history_set_interval(int s);   // 5/10/30 only; clears buffer + persists; true if applied
void   history_clear();               // wipe all logged samples
// Keep only the most recent `keepSeconds` of samples; drop everything older. Valid no-op
// (still returns true) if less than that is logged. Returns false only on an unusable buffer.
bool   history_trim_keep_recent(unsigned long keepSeconds);

// Downsampled JSON for charting (<= maxPoints): {interval,n,ph,ec,temp,age}.
String history_json(size_t maxPoints);

// CSV (for chunked export): fill `out` with the i-th-oldest sample line
// "sec_ago,ph,ec_mS,temp\n"; returns length. (Header is emitted by the caller.)
size_t history_csv_line(size_t i, char* out, size_t max);
