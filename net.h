#pragma once

// Wi-Fi station mode + mDNS. Non-blocking: kicks off the connection in
// net_begin() and manages (re)connection state in net_tick().
void net_begin();
void net_tick();
bool net_connected();
