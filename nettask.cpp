#include "nettask.h"
#include "sensors.h"
#include "thingspeak.h"
#include "notify.h"
#include <Arduino.h>

// Stack must hold an mbedTLS handshake (alarm emails go out over HTTPS). 16 KB matches
// the loop-task allowance we previously needed for the same reason; with the work moved
// here, loop() no longer pays that cost on every iteration.
static const uint32_t NET_TASK_STACK = 16 * 1024;
static const uint32_t NET_TASK_PRIO  = 1;            // same as the Arduino loop task
static const uint32_t NET_TICK_MS    = 250;          // cadence to service timers/tests

static TaskHandle_t s_task = nullptr;

static void netTaskFn(void*) {
  for (;;) {
    SensorState s = sensors_snapshot();   // thread-safe copy of the latest reading
    notify_tick(s);                       // edge-triggered alarm POSTs (may block on a bad link)
    ts_tick(s);                           // periodic ThingSpeak upload (may block)
    vTaskDelay(pdMS_TO_TICKS(NET_TICK_MS));
  }
}

void nettask_begin() {
  if (s_task) return;
  // Pin to the application core (same core as loop). When this task blocks on a socket
  // it enters the Blocked state and the scheduler hands the core back to loop(), so the
  // long HTTP waits no longer stall sensing/dosing/UI.
  xTaskCreatePinnedToCore(netTaskFn, "nettask", NET_TASK_STACK, nullptr,
                          NET_TASK_PRIO, &s_task, APP_CPU_NUM);
  if (s_task) Serial.println(F("[nettask] outbound HTTP task started"));
  else        Serial.println(F("[nettask] FAILED to start task"));
}
