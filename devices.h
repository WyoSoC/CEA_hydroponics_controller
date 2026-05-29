#pragma once
#include <Arduino.h>
#include <Ezo_i2c.h>

// Sensors
extern Ezo_board PH;
extern Ezo_board EC;
extern Ezo_board RTD;

// Pumps (index 0..2 == pH_down / pH_up / nutrient)
extern Ezo_board PUMP_PH_DOWN;
extern Ezo_board PUMP_PH_UP;
extern Ezo_board PUMP_NUTRIENT;
extern Ezo_board* PUMPS[3];

// Unified table for raw addressing: 0=pH 1=EC 2=RTD 3=p1 4=p2 5=p3
extern Ezo_board* ALL_DEVICES[6];
extern const char* DEVICE_TOKENS[6];

// Power on all sensor ports + the I2C/STEMMA-QT rail, then start Wire.
void devices_begin();

// Map a console token ("ph","ec","rtd","p1","p2","p3") to an ALL_DEVICES index, or -1.
int device_index_from_token(const String& tok);
