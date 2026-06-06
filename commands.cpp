#include "commands.h"
#include "config.h"
#include "devices.h"
#include "control.h"
#include "sensors.h"
#include <Ezo_i2c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdarg.h>

static QueueHandle_t q = nullptr;
static CmdReporter   reporter = nullptr;
static char          lastAction[40] = {0};
static unsigned long lastActionMs = 0;
static float         totalDosed[3] = {0, 0, 0};   // cumulative forward mL since boot (acid, nutA, nutB)

void commands_begin() {
  q = xQueueCreate(16, sizeof(Command));
}

const char*   commands_last_action()    { return lastAction; }
unsigned long commands_last_action_ms() { return lastActionMs; }
float         commands_total_dosed(uint8_t i) { return i < 3 ? totalDosed[i] : 0; }

void commands_set_reporter(CmdReporter r) { reporter = r; }

// Print a result line to Serial and, if registered, to the reporter sink.
static void report(const char* fmt, ...) {
  char buf[96];
  va_list args; va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(F("[cmd] ")); Serial.println(buf);
  if (reporter) reporter(buf);
}

bool commands_enqueue(const Command& c) {
  if (!q) return false;
  return xQueueSend(q, &c, 0) == pdTRUE;
}

bool commands_pop(Command& c) {
  if (!q) return false;
  return xQueueReceive(q, &c, 0) == pdTRUE;
}

uint8_t commands_pending() {
  return q ? (uint8_t)uxQueueMessagesWaiting(q) : 0;
}

void commands_execute(const Command& c) {
  char buf[48] = {0};

  switch (c.type) {
    case CMD_DISPENSE: {
      if (c.target > 2) return;
      Ezo_board* p = PUMPS[c.target];
      p->send_cmd_with_num("d,", c.value);
      delay(EZO_READ_DELAY);
      p->receive_cmd(buf, sizeof(buf));
      snprintf(lastAction, sizeof(lastAction), "%s %.2fmL", p->get_name(), c.value);
      lastActionMs = millis();
      if (c.value > 0) totalDosed[c.target] += c.value;   // accumulate dosing total (forward only)
      sensors_pause(DOSE_SETTLE_MS);   // let pump EMI settle before next sensor read
      report("%s dispense %.2f mL => %s", p->get_name(), c.value, buf);
      break;
    }
    case CMD_PUMP_STOP: {
      if (c.target > 2) return;
      Ezo_board* p = PUMPS[c.target];
      p->send_cmd("x");
      delay(EZO_CMD_DELAY);
      p->receive_cmd(buf, sizeof(buf));
      snprintf(lastAction, sizeof(lastAction), "stop %s", p->get_name());
      lastActionMs = millis();
      report("%s STOP => %s", p->get_name(), buf);
      break;
    }
    case CMD_SET_AUTO:
      control_set_auto(c.value != 0);
      break;

    case CMD_RAW: {
      if (c.target > 5) return;
      Ezo_board* d = ALL_DEVICES[c.target];
      d->send_cmd(c.raw);
      delay(EZO_CAL_DELAY);
      d->receive_cmd(buf, sizeof(buf));
      report("%s <- '%s' => %s", d->get_name(), c.raw, buf);
      break;
    }
  }
}
