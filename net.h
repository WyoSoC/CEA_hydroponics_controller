#pragma once
#include <Arduino.h>

// Wi-Fi station mode + mDNS. Non-blocking: kicks off the connection in
// net_begin() and manages (re)connection state in net_tick().
void net_begin();
void net_tick();
bool net_connected();

bool   net_time_valid();   // true once NTP has synced
String net_time_str();     // "YYYY-MM-DD HH:MM:SS" local, or "uptime Ns" before sync
