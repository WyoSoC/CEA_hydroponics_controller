#include "console.h"
#include "config.h"
#include "devices.h"
#include "sensors.h"
#include "commands.h"
#include "control.h"
#include <Arduino.h>

static bool   streaming = true;
static String inbuf;

static void printHelp() {
  Serial.println(F("\n=== Commands ==="));
  Serial.println(F("  status            latest readings + state"));
  Serial.println(F("  stream on|off     live printout of each reading"));
  Serial.println(F("  d <1|2|3> <ml>    dispense ml from a pump (negative = reverse)"));
  Serial.println(F("  x [1|2|3]         stop a pump (no arg = all)"));
  Serial.println(F("  cal <dev> <cmd>   raw EZO command for calibration"));
  Serial.println(F("                    dev = ph|ec|rtd|p1|p2|p3"));
  Serial.println(F("                    e.g. cal ph Cal,mid,7.00   cal ec K,1.0"));
  Serial.println(F("                         cal ec Cal,dry        cal p1 Cal,9.6"));
  Serial.println(F("                         cal ph Cal,?          (query)"));
  Serial.println(F("  auto on|off       enable/disable autonomous dosing"));
  Serial.println(F("  help              this menu"));
}

void console_begin() { printHelp(); }
bool console_streaming() { return streaming; }

void print_reading_line(const SensorState& s) {
  Serial.print(F("[read] pH="));
  if (s.phValid) Serial.print(s.ph, 2); else Serial.print(F("--"));
  Serial.print(F("  EC="));
  if (s.ecValid) Serial.print(s.ec, 0); else Serial.print(F("--"));
  Serial.print(F(" uS/cm  T="));
  if (s.tempValid) Serial.print(s.tempC, 1); else Serial.print(F("--"));
  Serial.print(F(" C  | auto="));
  Serial.print(control_auto_enabled() ? F("on") : F("off"));
  Serial.print(F("  queue="));
  Serial.println(commands_pending());
}

static void handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  int sp = line.indexOf(' ');
  String cmd  = (sp < 0) ? line : line.substring(0, sp);
  String rest = (sp < 0) ? String("") : line.substring(sp + 1);
  rest.trim();
  cmd.toLowerCase();

  if (cmd == "help" || cmd == "?") {
    printHelp();
  }
  else if (cmd == "status" || cmd == "s") {
    print_reading_line(sensors_snapshot());
  }
  else if (cmd == "stream") {
    streaming = (rest == "on");
    Serial.printf("[console] streaming %s\n", streaming ? "on" : "off");
  }
  else if (cmd == "auto") {
    Command c; c.type = CMD_SET_AUTO; c.target = 0; c.value = (rest == "on") ? 1 : 0; c.raw[0] = 0;
    commands_enqueue(c);
  }
  else if (cmd == "d") {
    int sp2 = rest.indexOf(' ');
    if (sp2 < 0) { Serial.println(F("usage: d <1|2|3> <ml>")); return; }
    int pump = rest.substring(0, sp2).toInt();
    float ml = rest.substring(sp2 + 1).toFloat();
    if (pump < 1 || pump > 3) { Serial.println(F("pump must be 1..3")); return; }
    Command c; c.type = CMD_DISPENSE; c.target = (uint8_t)(pump - 1); c.value = ml; c.raw[0] = 0;
    if (commands_enqueue(c)) Serial.printf("[console] queued dispense %.2f mL, pump %d\n", ml, pump);
    else Serial.println(F("queue full"));
  }
  else if (cmd == "x") {
    if (rest.length() == 0) {
      for (uint8_t i = 0; i < 3; i++) { Command c; c.type = CMD_PUMP_STOP; c.target = i; c.value = 0; c.raw[0] = 0; commands_enqueue(c); }
      Serial.println(F("[console] stop-all queued"));
    } else {
      int pump = rest.toInt();
      if (pump < 1 || pump > 3) { Serial.println(F("pump must be 1..3")); return; }
      Command c; c.type = CMD_PUMP_STOP; c.target = (uint8_t)(pump - 1); c.value = 0; c.raw[0] = 0;
      commands_enqueue(c);
      Serial.printf("[console] stop pump %d queued\n", pump);
    }
  }
  else if (cmd == "cal") {
    int sp2 = rest.indexOf(' ');
    if (sp2 < 0) { Serial.println(F("usage: cal <dev> <ezo-cmd>")); return; }
    String devtok = rest.substring(0, sp2);
    String ezocmd = rest.substring(sp2 + 1); ezocmd.trim();
    int idx = device_index_from_token(devtok);
    if (idx < 0) { Serial.println(F("dev = ph|ec|rtd|p1|p2|p3")); return; }
    if (ezocmd.length() == 0 || ezocmd.length() >= 24) { Serial.println(F("bad/long command")); return; }
    Command c; c.type = CMD_RAW; c.target = (uint8_t)idx; c.value = 0;
    ezocmd.toCharArray(c.raw, sizeof(c.raw));
    if (commands_enqueue(c)) Serial.printf("[console] queued: %s <- \"%s\"\n", devtok.c_str(), c.raw);
    else Serial.println(F("queue full"));
  }
  else {
    Serial.printf("unknown: %s  (type 'help')\n", cmd.c_str());
  }
}

void console_tick() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') { String l = inbuf; inbuf = ""; handleLine(l); }
    else { inbuf += ch; if (inbuf.length() > 120) inbuf = ""; }
  }
}
