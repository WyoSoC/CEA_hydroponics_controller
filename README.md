# CEA Hydroponics Controller

Firmware for an ESP32-based hydroponics controller used to monitor pH, electrical conductivity (EC), and solution temperature, and to control three Atlas Scientific EZO dosing pumps.

Target board:

- Adafruit ESP32-S3 TFT Feather

Primary devices:

- Atlas Scientific EZO pH circuit
- Atlas Scientific EZO EC circuit
- Atlas Scientific EZO RTD temperature circuit
- Three Atlas Scientific EZO pumps

## Features

- Non-blocking sensor polling for pH, EC, and temperature.
- Temperature-compensated pH and EC reads.
- Thread-safe command queue for pump and calibration commands.
- Manual pump dispense and stop commands from serial or web UI.
- Autonomous PI dose-and-wait control, disabled by default.
- Runtime-adjustable pH and EC settings persisted in ESP32 NVS.
- Embedded web dashboard with live readings over WebSocket.
- Public-control lock with tailnet/admin unlock flow.
- In-memory audit log for control actions.
- Tailnet-only OTA firmware upload.
- Per-unit display name and mDNS hostname stored in NVS.
- Sensor-read robustness: per-read retries, I2C bus recovery, post-dose settle, fail counters.
- Local data logging to a PSRAM ring buffer (default 7 days @ 10 s) with chart + CSV export.
- ThingSpeak cloud upload (durable history; on by default at 60 s once a key is set).
- Alarm notifications by email (shared relay) or webhook, NTP-timestamped, state-change only.
- Guided FOPDT auto-tune that proposes Kp/Ki via SIMC.
- On-board ST7789 TFT status screen; NTP local-time clock.

See `docs/firmware_reference.md` for the full module map, HTTP/WebSocket API, NVS keys, and config constants.

## Hardware Roles

Pump assignment in the firmware:

| Pump | EZO Address | Role |
| --- | --- | --- |
| 1 | `0x38` | Acid / pH down |
| 2 | `0x39` | Nutrient A |
| 3 | `0x3A` | Nutrient B |

Sensor addresses:

| Sensor | EZO Address |
| --- | --- |
| pH | `99` |
| EC | `100` |
| RTD | `102` |

Verify pump tubing and chemistry before enabling autonomous dosing.

## Repository Layout

| File | Purpose |
| --- | --- |
| `hydroponics_controller.ino` | Arduino setup/loop orchestration |
| `config.h` | Pins, I2C addresses, timing, defaults, safety limits |
| `devices.*` | EZO board objects and power/I2C initialization |
| `sensors.*` | Non-blocking sensor polling state machine |
| `commands.*` | FreeRTOS command queue and I2C command executor |
| `control.*` | Autonomous PI dosing logic |
| `settings.*` | Persistent runtime settings in NVS |
| `history.*` | PSRAM data-log ring buffer; JSON + chunked CSV |
| `notify.*` | Alarm notifier (email-via-relay or webhook) |
| `thingspeak.*` | Periodic ThingSpeak cloud upload |
| `tune.*` | Guided FOPDT auto-tune → SIMC gains |
| `display.*` | On-board ST7789 TFT status screen |
| `console.*` | Serial console commands |
| `net.*` | Wi-Fi, mDNS, and NTP time |
| `webserver.*` | HTTP API and WebSocket live updates |
| `access.*` | Public/tailnet access control and audit log |
| `ota.*` | OTA firmware upload handler |
| `identity.*` | Per-unit name and hostname |
| `index_html.h` | Embedded dashboard HTML/CSS/JS |
| `secrets.example.h` | Template for local credentials |

## Secrets

Real credentials are stored in `secrets.h`, which is intentionally ignored by Git.

Create it locally from the example:

```bash
cp secrets.example.h secrets.h
```

Then edit `secrets.h` with local deployment values:

```cpp
#define WIFI_SSID  "your-wifi-ssid"
#define WIFI_PASS  "your-wifi-password"
#define PROXY_SECRET "replace-with-long-random-secret"
```

Do not commit `secrets.h`.

## Build Requirements

Install:

- Arduino IDE or `arduino-cli`
- ESP32 Arduino core
- Atlas Scientific `Ezo_i2c` library
- `ESPAsyncWebServer`
- `AsyncTCP`

The current build target is:

```text
esp32:esp32:adafruit_feather_esp32s3_tft
```

The repository ignores generated build artifacts under `build/`.

## Operation

At boot, the firmware:

1. Powers sensor ports and starts I2C.
2. Loads settings and identity from NVS.
3. Starts the serial console.
4. Starts Wi-Fi station mode and mDNS.
5. Starts the HTTP server and WebSocket endpoint.
6. Begins periodic sensor polling.

The main loop advances sensor polling first. Pump, stop, calibration, and raw EZO commands are executed only when the sensor poller is idle, so web/serial actions do not touch I2C concurrently with sensor reads.

## Serial Console

Serial monitor:

```text
115200 baud
```

Useful commands:

```text
status
stream on|off
d <1|2|3> <ml>
x [1|2|3]
cal <ph|ec|rtd|p1|p2|p3> <ezo-command>
auto on|off
help
```

Examples:

```text
d 1 1.0
x
cal ph Cal,mid,7.00
cal ec Cal,dry
auto on
```

## Web UI

When connected to Wi-Fi, the device prints its IP and mDNS URL to serial:

```text
http://<hostname>.local/
```

The web UI provides:

- Live pH, EC, and temperature readings.
- Alarm indicators.
- Manual pump controls.
- PI control settings.
- Calibration wizard.
- Raw EZO command entry.
- Instructor/admin panel for lock control, audit log, OTA, and identity changes.

## Access Control

The controller starts locked on every boot.

- Tailnet/direct LAN origin: can control pumps, toggle lock, view audit log, rename device, and upload OTA firmware.
- Public/proxied origin: can control only while unlocked.
- OTA upload is always tailnet-only.

For proxied deployments, the reverse proxy should inject:

```text
X-Proxy-Secret: <matching PROXY_SECRET>
X-Access: tailnet
```

Requests with the proxy secret but without `X-Access: tailnet` are treated as public.

## Autonomous Control

Autonomous dosing is disabled by default and can be toggled from the serial console or web UI.

Control behavior:

- pH channel doses acid only when pH is above the pH setpoint.
- EC channel doses nutrient A and nutrient B equally only when EC is below the EC setpoint.
- Both channels use PI dose-and-wait logic with deadbands, max dose clamps, and persisted settings.
- Acid dosing is capped by `MAX_ACID_DOSES_PER_HOUR`.

Start with conservative gains and verify response manually before enabling autonomous operation.

## Development Notes

Before committing or pushing, verify secrets remain ignored:

```bash
git check-ignore -v secrets.h build/ .DS_Store
git status --short --ignored
```

Typical Git workflow:

```bash
git status
git add <files>
git commit -m "Describe the change"
git push
```
