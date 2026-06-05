#include "sensors.h"
#include "devices.h"
#include "config.h"
#include <Ezo_i2c.h>
#include <Wire.h>

// Non-blocking poll cycle with per-read retries + bus recovery:
//   START -> send RTD read
//   WAIT_RTD  -> receive temp (retry on I2C failure), then read pH & EC
//   WAIT_PHEC -> receive pH & EC (retry the ones that failed), publish state
//   GAP       -> idle (safe to run queued commands); also honors sensors_pause()
enum Step { P_START, P_WAIT_RTD, P_WAIT_PHEC, P_GAP };

static Step          step      = P_START;
static unsigned long stepStart = 0;
static unsigned long waitMs    = 0;
static unsigned long pauseUntil = 0;     // defer polling until this millis (dose settle)

static SensorState st = { NAN, NAN, NAN, false, false, false, 0 };
static bool  newReady = false;
static float pendTemp = TEMP_DEFAULT;
static bool  pendTempValid = false;

static int rtdTry = 0, phecTry = 0;      // remaining retries this cycle
static bool phGot = false, ecGot = false;
static float phVal = NAN, ecVal = NAN;
static int phFails = 0, ecFails = 0, tempFails = 0;   // consecutive failures
static int busFailCycles = 0;

static bool elapsed() { return millis() - stepStart >= waitMs; }
static void go(Step s, unsigned long w) { step = s; stepStart = millis(); waitMs = w; }

static const char* errStr(Ezo_board::errors e) {
  switch (e) {
    case Ezo_board::SUCCESS:      return "ok";
    case Ezo_board::FAIL:         return "FAIL";
    case Ezo_board::NOT_READY:    return "NOT_READY";
    case Ezo_board::NO_DATA:      return "NO_DATA";
    default:                      return "?";
  }
}

static void i2c_recover() {
  Serial.println(F("[i2c] bus recovery: reinit Wire"));
  Wire.end();
  delay(5);
  Wire.begin();
}

void sensors_begin() { go(P_START, 0); }
void sensors_pause(unsigned long ms) { unsigned long u = millis() + ms; if (u > pauseUntil) pauseUntil = u; }
int  sensors_fail_ph()   { return phFails; }
int  sensors_fail_ec()   { return ecFails; }
int  sensors_fail_temp() { return tempFails; }

void sensors_tick() {
  switch (step) {
    case P_START:
      RTD.send_read_cmd();
      rtdTry = SENSOR_READ_RETRIES;
      go(P_WAIT_RTD, EZO_READ_DELAY);
      break;

    case P_WAIT_RTD: {
      if (!elapsed()) break;
      Ezo_board::errors e = RTD.receive_read_cmd();
      if (e != Ezo_board::SUCCESS && rtdTry > 0) {      // transient -> retry
        rtdTry--; Serial.printf("[sensor] RTD %s (retry)\n", errStr(e));
        RTD.send_read_cmd(); go(P_WAIT_RTD, EZO_READ_DELAY); break;
      }
      pendTemp = TEMP_DEFAULT; pendTempValid = false;
      if (e == Ezo_board::SUCCESS) {
        float t = RTD.get_last_received_reading();
        if (t > -1000.0f) { pendTemp = t; pendTempValid = true; tempFails = 0; }
      } else { tempFails++; Serial.printf("[sensor] RTD %s (drop, #%d)\n", errStr(e), tempFails); }

      PH.send_read_with_temp_comp(pendTemp);
      EC.send_read_with_temp_comp(pendTemp);
      phGot = ecGot = false; phVal = ecVal = NAN; phecTry = SENSOR_READ_RETRIES;
      go(P_WAIT_PHEC, EZO_READ_DELAY);
      break;
    }

    case P_WAIT_PHEC: {
      if (!elapsed()) break;
      Ezo_board::errors ep = Ezo_board::SUCCESS, ee = Ezo_board::SUCCESS;
      if (!phGot) { ep = PH.receive_read_cmd(); if (ep == Ezo_board::SUCCESS) { phVal = PH.get_last_received_reading(); phGot = true; } }
      if (!ecGot) { ee = EC.receive_read_cmd(); if (ee == Ezo_board::SUCCESS) { ecVal = EC.get_last_received_reading(); ecGot = true; } }

      if (!(phGot && ecGot) && phecTry > 0) {            // retry whichever didn't arrive
        phecTry--;
        if (!phGot) { Serial.printf("[sensor] pH %s (retry)\n", errStr(ep)); PH.send_read_with_temp_comp(pendTemp); }
        if (!ecGot) { Serial.printf("[sensor] EC %s (retry)\n", errStr(ee)); EC.send_read_with_temp_comp(pendTemp); }
        go(P_WAIT_PHEC, EZO_READ_DELAY); break;
      }

      // finalize (validity = received OK AND within sanity range)
      st.tempC = pendTemp; st.tempValid = pendTempValid;
      st.phValid = false; st.ecValid = false;
      if (phGot) { st.ph = phVal; st.phValid = (phVal >= PH_MIN && phVal <= PH_MAX); phFails = 0; }
      else       { phFails++; Serial.printf("[sensor] pH %s (drop, #%d)\n", errStr(ep), phFails); }
      if (ecGot) { st.ec = ecVal; st.ecValid = (ecVal >= EC_MIN && ecVal <= EC_MAX); ecFails = 0; }
      else       { ecFails++; Serial.printf("[sensor] EC %s (drop, #%d)\n", errStr(ee), ecFails); }

      st.lastUpdateMs = millis();
      newReady = true;

      // if the whole bus is dark for several cycles, try to recover it
      if (!st.phValid && !st.ecValid && !st.tempValid) {
        if (++busFailCycles >= BUS_RECOVER_CYCLES) { i2c_recover(); busFailCycles = 0; }
      } else busFailCycles = 0;

      go(P_GAP, POLL_GAP);
      break;
    }

    case P_GAP:
      if (elapsed() && millis() >= pauseUntil) go(P_START, 0);
      break;
  }
}

bool sensors_idle()        { return step == P_GAP; }
bool sensors_consume_new() { if (newReady) { newReady = false; return true; } return false; }
SensorState sensors_snapshot() { return st; }
