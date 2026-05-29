#include "devices.h"
#include "config.h"
#include <Wire.h>

Ezo_board PH  = Ezo_board(ADDR_PH,  "pH");
Ezo_board EC  = Ezo_board(ADDR_EC,  "EC");
Ezo_board RTD = Ezo_board(ADDR_RTD, "RTD");

// Pump roles — VERIFY PHYSICALLY (use pump_selftest) before connecting chemicals.
//   0x38 = acid (pH down)   0x39 = nutrient A   0x3A = nutrient B
Ezo_board PUMP_PH_DOWN  = Ezo_board(ADDR_PUMP_PH_DOWN,  "acid");
Ezo_board PUMP_PH_UP    = Ezo_board(ADDR_PUMP_PH_UP,    "nutA");
Ezo_board PUMP_NUTRIENT = Ezo_board(ADDR_PUMP_NUTRIENT, "nutB");

Ezo_board* PUMPS[3]       = { &PUMP_PH_DOWN, &PUMP_PH_UP, &PUMP_NUTRIENT };
Ezo_board* ALL_DEVICES[6] = { &PH, &EC, &RTD, &PUMP_PH_DOWN, &PUMP_PH_UP, &PUMP_NUTRIENT };
const char* DEVICE_TOKENS[6] = { "ph", "ec", "rtd", "p1", "p2", "p3" };

void devices_begin() {
  pinMode(EN_PH,  OUTPUT); digitalWrite(EN_PH,  LOW);
  pinMode(EN_EC,  OUTPUT); digitalWrite(EN_EC,  LOW);
  pinMode(EN_RTD, OUTPUT); digitalWrite(EN_RTD, HIGH);   // RTD port is active-HIGH
  pinMode(EN_AUX, OUTPUT); digitalWrite(EN_AUX, LOW);

  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);   // also powers the I2C/STEMMA-QT port
  delay(100);

  Wire.begin();
}

int device_index_from_token(const String& tok) {
  for (int i = 0; i < 6; i++)
    if (tok.equalsIgnoreCase(DEVICE_TOKENS[i])) return i;
  return -1;
}
