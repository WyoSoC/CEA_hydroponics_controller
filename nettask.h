#pragma once

// Dedicated FreeRTOS task for all *blocking* outbound HTTP (ThingSpeak uploads +
// alarm-notifier POSTs). These calls can block for seconds on a poor link and,
// when run inline from loop(), they stall sensor polling / dosing / the TFT and
// pile up work behind the async web stack — the suspected trigger for the PANIC
// reboots seen on the high-latency unit. Isolated on its own task with a generous
// (TLS-safe) stack, a blocked socket simply yields the CPU back to loop().
//
// Call once, after net_begin()/web_begin() and after ts_begin()/notify_begin().
void nettask_begin();
