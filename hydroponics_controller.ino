/*
 * Hydroponics controller — non-blocking core (step 1 of the web-app build).
 * Board: Adafruit ESP32-S3 TFT Feather   |   Serial Monitor @ 115200
 *
 * Architecture (modules in this folder):
 *   config.h    pins, addresses, timing, targets, safety limits
 *   devices.*   Ezo_board objects (3 sensors + 3 pumps) + power-on
 *   sensors.*   non-blocking poll state machine -> SensorState snapshot
 *   commands.*  thread-safe command queue + executor (the ONLY place that
 *               touches I2C for actions). The web server will enqueue here too.
 *   control.*   autonomous dosing decisions (default OFF) -> enqueues doses
 *   console.*   serial UI: status, manual dosing, raw calibration commands
 *
 * Why this shape: EZO reads block ~1 s. Polling runs as a cooperative state
 * machine so loop() never stalls, and all action commands run from a queue at a
 * safe point between cycles. An async web server (next step) can enqueue from
 * its own task without ever touching I2C concurrently.
 *
 * Requires the Atlas "Ezo_i2c" library.
 */

#include "config.h"
#include "devices.h"
#include "sensors.h"
#include "commands.h"
#include "control.h"
#include "settings.h"
#include "history.h"
#include "display.h"
#include "notify.h"
#include "tune.h"
#include "thingspeak.h"
#include "console.h"
#include "net.h"
#include "webserver.h"
#include "access.h"
#include "ota.h"
#include "identity.h"

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);   // wait briefly for USB serial

  devices_begin();
  sensors_begin();
  commands_begin();
  settings_begin();                     // load control settings from NVS
  identity_begin();                     // load per-unit name/hostname from NVS
  history_begin();                      // allocate data-log ring buffer (PSRAM)
  notify_begin();                       // load alarm-notifier webhook config from NVS
  ts_begin();                           // load ThingSpeak upload config from NVS
  tune_begin();                         // auto-tune state machine
  control_begin();
  console_begin();
  access_begin();                       // lock state (default LOCKED) + audit log
  net_begin();                          // Wi-Fi station (non-blocking)
  web_begin();                          // HTTP + WebSocket server
  display_begin();                      // on-board TFT status screen

  Serial.println(F("\nHydroponics controller — core + web ready. Type 'help'."));
}

void loop() {
  sensors_tick();                       // advance the poll state machine (non-blocking)

  if (sensors_idle()) {                 // between cycles -> safe to touch I2C for an action
    Command c;
    if (commands_pop(c)) commands_execute(c);
  }

  tune_tick(sensors_snapshot());        // auto-tune step-response (no-op unless running)
  control_tick(sensors_snapshot());     // autonomous dosing (no-op unless enabled)
  history_tick(sensors_snapshot());     // sample into the data log every LOG_INTERVAL_S
  notify_tick(sensors_snapshot());      // fire alarm notifications on transitions
  ts_tick(sensors_snapshot());          // periodic ThingSpeak upload
  display_tick(sensors_snapshot());     // refresh the TFT (~1 Hz internally)
  console_tick();                       // serial UI
  net_tick();                           // manage Wi-Fi (re)connection
  web_tick();                           // ws client cleanup
  access_tick();                        // auto-relock when an unlock expires

  if (access_consume_lock_changed())    // lock toggled/expired -> push new state to UIs
    web_broadcast();

  if (web_ota_pending_reboot()) {       // reboot after a successful OTA flash
    Serial.println(F("[ota] rebooting into new firmware..."));
    delay(1200);
    ESP.restart();
  }
  if (identity_consume_reboot()) {      // reboot to apply a new hostname/identity
    Serial.println(F("[identity] rebooting to apply new identity..."));
    delay(800);
    ESP.restart();
  }

  if (sensors_consume_new()) {          // one fresh reading -> fan out to serial + web
    SensorState s = sensors_snapshot();
    if (console_streaming()) print_reading_line(s);
    web_broadcast();
  }
}
