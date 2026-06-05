#include "display.h"
#include "identity.h"
#include "control.h"
#include "access.h"
#include "commands.h"
#include "net.h"
#include <Arduino.h>
#include <stdarg.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// Hardware SPI; pins come from the Adafruit Feather ESP32-S3 TFT board variant.
static Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
static unsigned long lastDraw = 0;
static uint16_t C_HOST, C_PH, C_EC, C_T, C_LBL, C_OK, C_WARN;

void display_begin() {
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);          // TFT_I2C_POWER already driven HIGH in devices_begin
  tft.init(135, 240);
  tft.setRotation(3);                        // landscape: 240 wide x 135 tall
  tft.fillScreen(ST77XX_BLACK);
  C_HOST = ST77XX_WHITE;
  C_PH   = tft.color565(243, 156, 18);
  C_EC   = tft.color565(52, 152, 219);
  C_T    = tft.color565(26, 182, 200);
  C_LBL  = tft.color565(120, 150, 165);
  C_OK   = tft.color565(39, 174, 96);
  C_WARN = tft.color565(231, 76, 60);
}

// Draw a fixed-width text field with opaque background (overwrites prior text -> no flicker).
static void field(int x, int y, uint8_t sz, uint16_t fg, const char* fmt, ...) {
  char b[40];
  va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a);
  tft.setTextSize(sz);
  tft.setTextColor(fg, ST77XX_BLACK);
  tft.setCursor(x, y);
  tft.print(b);
}

void display_tick(const SensorState& s) {
  unsigned long now = millis();
  if (now - lastDraw < 1000) return;         // ~1 Hz
  lastDraw = now;

  // IP on top, size 2 (y 2..18). Pad to 16 chars: 16*12 = 192 px < 240 -> never off-screen.
  field(4,  2, 2, C_HOST, "%-16.16s", net_connected() ? WiFi.localIP().toString().c_str() : "connecting...");
  // hostname, size 1 (y 22..30)
  field(4, 22, 1, C_LBL,  "%-26.26s", identity_host());

  // readings, size 2 (each 16 px tall): pH 36, EC 56, T 76
  if (s.phValid)   field(4, 36, 2, C_PH, "pH  %5.2f   ", s.ph);        else field(4, 36, 2, C_PH, "pH    --    ");
  if (s.ecValid)   field(4, 56, 2, C_EC, "EC %5.2f mS ", s.ec/1000.0); else field(4, 56, 2, C_EC, "EC   -- mS  ");
  if (s.tempValid) field(4, 76, 2, C_T,  "T  %5.1f C  ", s.tempC);     else field(4, 76, 2, C_T,  "T    -- C   ");

  // status + last action, size 1
  field(4,  98, 1, access_locked() ? C_WARN : C_OK, "%-7s", access_locked() ? "LOCKED" : "UNLOCK");
  field(80, 98, 1, control_auto_enabled() ? C_OK : C_LBL, "auto %-3s", control_auto_enabled() ? "ON" : "off");

  unsigned long aMs = commands_last_action_ms();
  if (aMs) field(4, 114, 1, C_LBL, "last %-14s %lus ", commands_last_action(), (now - aMs) / 1000);
  else     field(4, 114, 1, C_LBL, "last --             ");
}
