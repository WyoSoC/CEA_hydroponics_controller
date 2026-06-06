# Hydroponics Controller — Firmware Reference

Current reference for the `hydroponics_controller` firmware (Adafruit ESP32-S3 TFT Feather).
Covers the module map, runtime architecture, the full HTTP/WebSocket API, persisted
settings (NVS), key config constants, and the deploy artifacts.

> For a teaching walkthrough see `hydroponics_control_tutorial.md`. For setup/build steps
> see the repo `README.md` and `hydroponics_controller/README.md`.

---

## 1. Hardware

| Device | I2C addr | Role |
|---|---|---|
| EZO pH | `0x63` | pH |
| EZO EC | `0x64` | conductivity (µS/cm; UI shows mS/cm) |
| EZO RTD | `0x66` | temperature (compensates pH/EC) |
| Tri-PMP pump 1 | `0x38` | **acid** (lowers pH) |
| Tri-PMP pump 2 | `0x39` | **nutrient A** (raises EC) |
| Tri-PMP pump 3 | `0x3A` | **nutrient B** (raises EC) |
| MAX17048 | `0x36` | onboard battery gauge (ignored) |

Sensor-port enable pins (kit PCB): `EN_PH=12`, `EN_EC=11`, `EN_AUX=10` (active **LOW**),
`EN_RTD=9` (active **HIGH**). `TFT_I2C_POWER` must be HIGH to power the I2C/STEMMA rail
**and** the TFT. The Tri-PMP motors run on their own 24 V supply; only SDA/SCL/GND join the bus.

---

## 2. Runtime architecture

Everything is **non-blocking and single-I2C-owner**:

- `sensors_tick()` runs a cooperative poll state machine (RTD → temp-compensated pH/EC → gap),
  with per-read retries, an I2C bus-recovery on repeated failure, and a configurable
  post-dose settle pause.
- All actions (dose, stop, calibrate, set-auto) are placed on a **FreeRTOS command queue**
  and executed only at a safe point between poll cycles — so the async web server can enqueue
  from its own task without ever touching I2C concurrently.
- Periodic/event work (`control_tick`, `history_tick`, `tune_tick`, `display_tick`) runs from
  `loop()` and is internally rate-gated.
- **Blocking outbound HTTP** (`notify_tick`, `ts_tick`) runs on a **dedicated FreeRTOS task**
  (`nettask`, pinned to the app core with a 16 KB TLS-safe stack). A slow/poor link can block
  these for seconds; isolating them keeps `loop()` (sensing, dosing, TFT) responsive and avoids
  starving the async web stack — the suspected PANIC trigger on a high-latency unit.

### Module map

| File | Responsibility |
|---|---|
| `hydroponics_controller.ino` | `setup()`/`loop()` orchestration; reboot-on-flag (OTA, identity). |
| `config.h` | All compile-time constants (pins, addresses, timing, PI defaults, NTP/TZ, limits). |
| `secrets.h` | Wi-Fi creds, `EMAIL_RELAY_URL`, `PROXY_SECRET`. Gitignored (`secrets.example.h` is the template). |
| `devices.*` | `Ezo_board` objects + power-on + device-token lookup. |
| `sensors.*` | Non-blocking poll, retries, bus recovery, fail counters, `sensors_pause()`. |
| `commands.*` | Thread-safe command queue + executor; last-pump-action; result reporter. |
| `control.*` | Per-channel PI dose-and-wait controller. |
| `settings.*` | Runtime PI setpoints/gains/intervals/alarms, persisted in NVS. |
| `identity.*` | Per-unit name + mDNS hostname in NVS (MAC-derived default). |
| `history.*` | PSRAM ring-buffer data log; JSON (downsampled) + chunked CSV. |
| `net.*` | Wi-Fi station, mDNS, **NTP time** (`net_time_str/valid`). |
| `access.*` | Request-origin classification, the public-control **lock**, audit log. |
| `notify.*` | Alarm notifier (email-via-relay or webhook), NTP-timestamped. |
| `thingspeak.*` | Periodic cloud upload of readings. |
| `nettask.*` | Dedicated FreeRTOS task running the blocking outbound HTTP (`notify_tick`/`ts_tick`) off `loop()`. |
| `tune.*` | Guided FOPDT auto-tune → SIMC gains (propose-and-apply). |
| `ota.*` | Tailnet-only OTA firmware upload. |
| `display.*` | On-board ST7789 TFT status screen. |
| `webserver.*` | Async HTTP + WebSocket; all `/api/*` routes. |
| `console.*` | Serial command UI. |
| `index_html.h` | Embedded dashboard (PROGMEM). |

---

## 3. Control model (PI, dose-and-wait)

- **pH**: acid only (pH drifts up on its own; no base pump). Dose acid when `pH > setpoint`.
- **EC**: nutrients only (pumps 2 & 3, equal dose). Dose when `EC < setpoint`.
- Per channel: `dose = Kp·e + Ki·Σ(e·Δt)`, **clamped one-directional** to `[0, maxDose]`,
  acted **once per settle interval** (dose, then wait to mix and re-measure).
- Anti-windup: integrator floored at 0, conditional integration when saturated, bled down on
  the overshoot side; integrators reset on enable (bumpless).
- **No derivative** (large dead time + noisy probes). Autonomous dosing defaults **OFF** and
  **resets OFF on every reboot** (fail-safe) — re-enable after a restart.
- Auto-tune (`tune.*`) doses one bolus, fits FOPDT (two-point 28.3 %/63.2 %), computes
  `Kp/Ki` via SIMC (λ = 2·L, no-overshoot), and **proposes** them for review.

---

## 4. Access control & the lock

Requests are classified by headers a reverse proxy injects:

- `X-Proxy-Secret` must equal `PROXY_SECRET` for the proxy tag to be trusted.
- `X-Access: tailnet` → **tailnet** origin; otherwise (valid secret) → **public**.
- **No proxy secret → treated as tailnet** (direct LAN/tailnet; the device isn't publicly routable).

The **lock** (RAM, default LOCKED, re-locks on boot, auto-expires):

- tailnet → may always control **and** toggle the lock.
- public → may control **only while unlocked**; can never toggle.

Guardrails apply to everyone: dose clamp, id/command validation, audit log.

---

## 5. HTTP / WebSocket API

`GET /` serves the dashboard (`Cache-Control: no-store`). `GET /ws` is the WebSocket.

| Method & path | Auth | Purpose |
|---|---|---|
| `GET /api/whoami` | any | `{origin, secretOk, fw, build, name}` (`fw` = `x.xx` version from `version.h`, `build` = compile timestamp) |
| `POST /api/lock` | tailnet | lock public control |
| `POST /api/unlock?min=N` | tailnet | unlock for N minutes (default 60, cap 240) |
| `POST /api/pump?id=1..3&ml=` | control | dispense (clamped to ±`MANUAL_MAX_DOSE_ML`) |
| `POST /api/pump/stop[?id=1..3]` | control | stop one pump or all |
| `POST /api/cal?dev=ph\|ec\|rtd\|p1\|p2\|p3&cmd=` | control | raw EZO command (calibration) |
| `POST /api/auto?on=1\|0` | control | autonomous dosing on/off |
| `GET /api/settings` / `POST /api/settings` | any / control | read / update PI settings |
| `GET /api/log` | tailnet | audit log (JSON) |
| `GET /api/identity` / `POST /api/identity?name=&host=` | any / tailnet | read / set unit identity (reboots) |
| `GET /api/history?n=600` | any | downsampled chart data |
| `GET /api/history.csv` | any | full chunked CSV export |
| `POST /api/loginterval?s=1..3600` | tailnet | set log interval (clears buffer) |
| `POST /api/history/clear` | tailnet | wipe the in-memory log |
| `GET /api/notify` / `POST /api/notify?on=&url=` / `POST /api/notify/test` | tailnet | alarm-notifier config + test |
| `GET /api/thingspeak` / `POST /api/thingspeak?on=&key=&interval=` / `POST /api/thingspeak/test` | tailnet | cloud-upload config + test |
| `GET /api/tune` / `POST /api/tune/start?ch=ph\|ec` / `POST /api/tune/apply` / `POST /api/tune/abort` | any / control | auto-tune status + control |
| `POST /api/ota` | tailnet | OTA firmware upload (multipart, field `firmware`) |

### WebSocket `/ws`

The server pushes a JSON **state** object on connect and on each reading:

```
{ name, ph, phOk, ec, ecOk, temp, tempOk, ageMs, auto, queue,
  locked, lockRemain, uptime,
  phLo, phHi, ecLo, ecHi,                              // alarm flags
  phSp, phAlo, phAhi, phKp, phKi, phMin,               // pH settings
  ecSp, ecAlo, ecAhi, ecKp, ecKi, ecMin,               // EC settings (mS/cm)
  iPh, iEc,                                            // PI integrator terms
  phF, ecF, tF,                                        // consecutive sensor-fail counts
  dAcid, dNutA, dNutB }                                // mL dosed per pump since boot
```

`ec` is in µS/cm (the dashboard divides by 1000 for mS/cm). Command results are pushed as
`{"event":"..."}` messages.

---

## 6. Alarm notifications

`notify.*` emails/posts **only on a state change** — one message when an alarm is entered,
one when it returns to normal (no periodic re-sending). The Alerts field accepts:

- an **email address** (has `@`) → relayed via `EMAIL_RELAY_URL` (a Google Apps Script web app
  from `deploy/email_relay.gs`) with the address as `to`; or
- a **webhook URL** (`http…`) → POSTed directly (e.g. `https://ntfy.sh/<topic>`).

Messages carry NTP-synced **timing**: `… | triggered <time>` and `… | cleared <time>, lasted <dur>`.
POST payload: `{to, unit, status, alarms, time, msg, ph, ec, temp}`. Apps Script POSTs are
302-redirected, so the client uses `HTTPC_FORCE_FOLLOW_REDIRECTS`; HTTPS uses `setInsecure()`.

---

## 7. Local logging + cloud

- **History** (`history.*`): PSRAM ring buffer, 8-byte records, implicit timestamps. Default
  7 days @ 10 s (~473 KB) — **requires Tools→PSRAM enabled**; falls back to ~1 day in internal
  RAM. **Volatile** (cleared on reboot/OTA). Interval is runtime-settable 1–3600 s. Chart +
  CSV in the dashboard.
- **ThingSpeak** (`thingspeak.*`): durable cloud history. **Enabled by default at 60 s**
  (no-op until a Write API Key is set). Fields 1=pH, 2=EC mS/cm, 3=temp.

---

## 8. Persisted settings (NVS)

| Namespace | Keys |
|---|---|
| `hydro` | `phSp phAlo phAhi phKp phKi phMin ecSp ecAlo ecAhi ecKp ecKi ecMin` |
| `ident` | `name host` |
| `loghz` | `s` (log interval, seconds) |
| `notify` | `url` (email or webhook) , `on` |
| `ts` | `key` (Write API Key), `on`, `int` |

NVS survives reboot and OTA. It is only wiped by a **partition-table change** (a USB flash
with a different partition scheme) or "Erase All Flash Before Sketch Upload".

---

## 9. Selected config constants (`config.h`)

`PH_*/EC_*` enable pins & addresses · `EZO_READ_DELAY`, `POLL_GAP` · PI defaults
`DEF_PH_SETPOINT 5.9`, `DEF_PH_KP 1.0`, `DEF_PH_MIN 10`, `DEF_EC_SETPOINT 1.9`, `DEF_EC_KP 2.0`,
`DEF_EC_MIN 30` · `PH_MAX_DOSE_ML 2.0`, `EC_MAX_DOSE_ML 5.0`, `MIN_DOSE_ML 0.10`,
`MAX_ACID_DOSES_PER_HOUR 6` · sensor robustness `SENSOR_READ_RETRIES 2`, `DOSE_SETTLE_MS 3000`,
`BUS_RECOVER_CYCLES 5` · logging `LOG_INTERVAL_S 10`, `LOG_DAYS 7`, `LOG_INTERVAL_MIN/MAX 1/3600`
· `MANUAL_MAX_DOSE_ML 25`, `DEFAULT_UNLOCK_MIN 60`, `MAX_UNLOCK_MIN 240` · ThingSpeak
`TS_MIN_INTERVAL_S 15`, `TS_DEFAULT_INTERVAL_S 60` · time `NTP_SERVER1/2`, `TZ_INFO` (Mountain)
· auto-tune `TUNE_PH_DOSE_ML 0.5`, `TUNE_EC_DOSE_ML 1.0`, `TUNE_LAMBDA_FACTOR 2.0`.

---

## 10. Build & deploy

- **Board:** Adafruit Feather ESP32-S3 TFT. **Partition Scheme:** an OTA-capable layout with
  ≥1.9 MB APP (e.g. *Minimal SPIFFS (1.9MB APP with OTA)*). **PSRAM:** enabled (for 7-day log).
- **Libraries:** `Ezo_i2c` (Atlas), `ESPAsyncWebServer` + `AsyncTCP` (ESP32Async),
  `Adafruit GFX` + `Adafruit ST7735/ST7789` + `Adafruit BusIO`.
- **Updates:** flash once over USB with the OTA partition; thereafter use the dashboard OTA or
  `deploy/ota_flash_all.sh` to push one `.bin` to every unit.

Deploy artifacts in `deploy/`:

| File | Purpose |
|---|---|
| `Caddyfile.test` | Reverse-proxy test config (public + tailnet routes injecting `X-Access`). |
| `email_relay.gs` | Google Apps Script email relay (`doPost` reads `to`; `doGet` health check). |
| `ota_flash_all.sh` | Flash one firmware `.bin` to all units over OTA (checks tailnet origin, prints `fw` version). |
| `bump_version.sh` | Increment `version.h` (`x.xx`, +0.01) before each build; the version shows in the dashboard footer and `/api/whoami`. |
