#pragma once
#include <Arduino.h>

// A unit of work that touches I2C. Enqueued from any context (serial now, the
// async web server later) and executed only from the main loop at a safe point,
// so I2C is never touched concurrently.
enum CmdType : uint8_t {
  CMD_RAW,        // send raw EZO command string `raw` to ALL_DEVICES[target]
  CMD_DISPENSE,   // PUMPS[target].d,<value mL>   (negative value = reverse)
  CMD_PUMP_STOP,  // PUMPS[target].x
  CMD_SET_AUTO    // enable(value!=0)/disable autonomous dosing
};

struct Command {
  CmdType type;
  uint8_t target;    // pump index 0..2, or device index 0..5 for CMD_RAW
  float   value;     // mL, or 0/1 for CMD_SET_AUTO
  char    raw[24];   // EZO command string for CMD_RAW (e.g. "Cal,mid,7.00")
};

// Optional sink for human-readable command results (e.g. broadcast to the web UI).
typedef void (*CmdReporter)(const char* msg);

void    commands_begin();                  // create the queue
void    commands_set_reporter(CmdReporter r); // register a result sink (or nullptr)
bool    commands_enqueue(const Command& c);// thread-safe; false if full
bool    commands_pop(Command& c);          // non-blocking
void    commands_execute(const Command& c);// runs the I2C exchange (main loop only)
uint8_t commands_pending();                // queued count

// Last pump action (for the TFT). e.g. "acid 0.50mL" / "stop nutA". 0 ms = none yet.
const char*   commands_last_action();
unsigned long commands_last_action_ms();
