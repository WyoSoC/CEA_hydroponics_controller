#include "sensors.h"
#include "devices.h"
#include "config.h"
#include <Ezo_i2c.h>

// Non-blocking poll cycle:
//   START -> send RTD read
//   WAIT_RTD  (EZO_READ_DELAY) -> receive temp, push temp-comp + read pH & EC
//   WAIT_PHEC (EZO_READ_DELAY) -> receive pH & EC, publish state
//   GAP       (POLL_GAP)       -> idle window (safe to run queued commands)
enum Step { P_START, P_WAIT_RTD, P_WAIT_PHEC, P_GAP };

static Step          step      = P_START;
static unsigned long stepStart = 0;
static unsigned long waitMs    = 0;

static SensorState st = { NAN, NAN, NAN, false, false, false, 0 };
static bool  newReady = false;
static float pendTemp = TEMP_DEFAULT;
static bool  pendTempValid = false;

static bool elapsed() { return millis() - stepStart >= waitMs; }
static void go(Step s, unsigned long w) { step = s; stepStart = millis(); waitMs = w; }

void sensors_begin() { go(P_START, 0); }

void sensors_tick() {
  switch (step) {
    case P_START:
      RTD.send_read_cmd();
      go(P_WAIT_RTD, EZO_READ_DELAY);
      break;

    case P_WAIT_RTD:
      if (!elapsed()) break;
      RTD.receive_read_cmd();
      pendTemp = TEMP_DEFAULT;
      pendTempValid = false;
      if (RTD.get_error() == Ezo_board::SUCCESS) {
        float t = RTD.get_last_received_reading();
        if (t > -1000.0f) { pendTemp = t; pendTempValid = true; }
      }
      // read pH & EC temperature-compensated (sends "RT,<temp>")
      PH.send_read_with_temp_comp(pendTemp);
      EC.send_read_with_temp_comp(pendTemp);
      go(P_WAIT_PHEC, EZO_READ_DELAY);
      break;

    case P_WAIT_PHEC:
      if (!elapsed()) break;
      PH.receive_read_cmd();
      EC.receive_read_cmd();

      st.tempC = pendTemp;
      st.tempValid = pendTempValid;

      st.phValid = false;
      if (PH.get_error() == Ezo_board::SUCCESS) {
        float v = PH.get_last_received_reading();
        st.ph = v;
        st.phValid = (v >= PH_MIN && v <= PH_MAX);
      }
      st.ecValid = false;
      if (EC.get_error() == Ezo_board::SUCCESS) {
        float v = EC.get_last_received_reading();
        st.ec = v;
        st.ecValid = (v >= EC_MIN && v <= EC_MAX);
      }

      st.lastUpdateMs = millis();
      newReady = true;
      go(P_GAP, POLL_GAP);
      break;

    case P_GAP:
      if (elapsed()) go(P_START, 0);
      break;
  }
}

bool sensors_idle()        { return step == P_GAP; }
bool sensors_consume_new() { if (newReady) { newReady = false; return true; } return false; }
SensorState sensors_snapshot() { return st; }
