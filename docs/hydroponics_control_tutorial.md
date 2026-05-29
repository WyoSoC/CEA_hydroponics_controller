---
marp: true
title: Hydroponics Control Tutorial
description: Student tutorial for the CEA hydroponics controller firmware
paginate: true
theme: default
---

# Hydroponics Control Tutorial

## Monitoring, Calibration, Data Collection, Analysis, and Testing

CEA Hydroponics Controller

---

# Learning Goals

By the end of this tutorial, students should be able to:

- Explain why pH, EC, and temperature matter in controlled environment agriculture.
- Describe how the controller reads sensors and actuates dosing pumps.
- Use the web UI and serial console safely.
- Calibrate pH, EC, temperature, and pumps.
- Collect and interpret controller data.
- Design tests that verify sensor, actuator, and control behavior.

---

# What Is CEA?

Controlled Environment Agriculture (CEA) uses sensors, actuators, and control systems to manage crop conditions.

Common controlled variables:

- Light
- Temperature
- Humidity
- CO2
- Airflow
- Irrigation
- Nutrient chemistry

This controller focuses on nutrient solution chemistry.

---

# Why Hydroponic Control Matters

Plants do not directly "see" the controller. They respond to the root-zone environment.

Important root-zone variables:

- pH affects nutrient availability.
- EC estimates total dissolved nutrient concentration.
- Temperature affects roots, oxygen availability, and sensor compensation.
- Pump dose volume determines how chemistry changes over time.

Good control depends on good measurement.

---

# Controller Inputs and Outputs

Inputs:

- pH probe
- EC probe
- RTD temperature probe
- User commands from web UI or serial console

Outputs:

- Pump 1: acid / pH down
- Pump 2: nutrient A
- Pump 3: nutrient B
- Web dashboard updates
- Serial status messages

---

# Hardware Map

| Device | Role |
| --- | --- |
| ESP32-S3 Feather | Runs firmware, Wi-Fi, web UI |
| Atlas EZO pH | Measures acidity |
| Atlas EZO EC | Measures conductivity |
| Atlas EZO RTD | Measures solution temperature |
| EZO Pump 1 | Adds acid |
| EZO Pump 2 | Adds nutrient A |
| EZO Pump 3 | Adds nutrient B |

All Atlas devices communicate over I2C.

---

# What The Firmware Does

The firmware continuously:

1. Reads temperature.
2. Uses temperature to compensate pH and EC readings.
3. Publishes live readings to the web UI.
4. Accepts commands from web or serial.
5. Queues commands that touch I2C.
6. Optionally computes autonomous dosing decisions.
7. Executes pump and calibration commands at safe times.

---

# Why Commands Are Queued

Atlas EZO reads take time. A reading can block for about one second.

If web, serial, sensor reads, and pump commands all touched I2C at once, communication could collide.

This controller avoids that by using:

- A sensor polling state machine.
- A command queue.
- A safe idle window between sensor cycles.

Only the command executor sends pump or raw EZO commands.

---

# Sensor Polling State Machine

The sensor loop is cooperative:

```text
START
  send RTD read
WAIT_RTD
  receive temperature
  send temperature-compensated pH and EC reads
WAIT_PHEC
  receive pH and EC
  publish snapshot
GAP
  idle window for queued commands
```

The main loop keeps running while the controller waits.

---

# Sensor Snapshot

Each completed reading creates a snapshot:

- pH value
- EC value
- Temperature value
- Valid/invalid flags
- Timestamp

Invalid readings can happen because of:

- Sensor communication failure
- Sensor out of solution
- Calibration issue
- Reading outside sanity limits

The control logic only acts on valid readings.

---

# Units

pH:

- Unitless acidity scale.
- Typical hydroponic target is often near pH 5.5 to 6.5.

EC:

- Probe reports conductivity in microSiemens per centimeter.
- Web UI displays EC as milliSiemens per centimeter.
- Conversion: `mS/cm = uS/cm / 1000`

Temperature:

- Degrees Celsius.
- Used for compensation.

---

# Web Dashboard

The dashboard provides:

- Live pH, EC, and temperature.
- Reading age and connection status.
- Alarm indicators.
- Manual pump dosing.
- PI control settings.
- Calibration wizard.
- Raw EZO command entry.
- Instructor/admin tools.

Live values are pushed over WebSocket.

---

# Access Control

The controller starts locked on every boot.

Tailnet or direct LAN:

- Can control pumps.
- Can unlock public control.
- Can view audit logs.
- Can rename the device.
- Can upload OTA firmware.

Public/proxied users:

- Can control only while unlocked.
- Cannot unlock the controller.
- Cannot upload firmware.

---

# Manual Pump Control

Manual dosing is useful for:

- Priming tubing.
- Testing pump direction.
- Verifying pump calibration.
- Making controlled chemistry adjustments.

Manual dose commands are clamped to a maximum single dose.

Negative dose values reverse the pump.

Always verify pump tubing before adding chemicals.

---

# Autonomous Control

Autonomous dosing is disabled by default.

When enabled:

- pH control adds acid only if pH is above the setpoint.
- EC control adds nutrient A and nutrient B equally if EC is below the setpoint.
- The controller doses, waits for mixing, then measures again.

The controller cannot remove acid or nutrient once added.

---

# PI Control Concept

The controller uses a PI dose calculation:

```text
dose = Kp * error + Ki * sum(error * time)
```

Proportional term:

- Reacts to the current error.
- Larger `Kp` gives stronger corrections.

Integral term:

- Accumulates persistent error.
- Larger `Ki` can remove steady offset.
- Too much `Ki` can cause overshoot.

---

# One-Directional Control

This system can only add chemicals.

pH:

- Acid lowers pH.
- There is no base pump in this controller.
- If too much acid is added, the controller cannot automatically undo it.

EC:

- Nutrients raise EC.
- The controller cannot remove nutrients.
- If EC is too high, dilution or reservoir replacement may be needed.

This is why conservative dosing matters.

---

# Dose-And-Wait Strategy

Hydroponic chemistry does not change instantly at the probe.

After a dose:

1. Pump adds chemical.
2. Solution mixes.
3. Probe reading changes.
4. Controller evaluates the next action.

Dose intervals prevent repeated dosing before the previous dose has mixed.

---

# Safety Limits

The firmware includes guardrails:

- Minimum dose threshold to avoid unreliable micro-doses.
- Maximum acid dose per cycle.
- Maximum nutrient dose per cycle.
- Acid dose count limit per hour.
- Sensor sanity ranges.
- Manual dose clamp.
- Public control lock.

Software guardrails do not replace physical supervision.

---

# Calibration Overview

Calibration aligns sensor and pump output with reality.

Calibrate:

- pH probe with known buffer solutions.
- EC probe with known conductivity standard.
- RTD probe only if needed.
- Pumps by measuring actual dispensed volume.

Bad calibration causes bad control decisions.

---

# pH Calibration

Typical pH calibration uses known buffers:

- Midpoint: pH 7.00
- Low point: pH 4.00
- High point: pH 10.00

Procedure:

1. Rinse probe.
2. Place in buffer.
3. Wait for stable reading.
4. Send calibration command.
5. Rinse before the next buffer.

Order: mid, low, high.

---

# EC Calibration

EC calibration depends on the probe K value and standard solution.

Typical steps:

1. Set probe K value.
2. Dry-calibrate with a dry probe.
3. Place probe in known EC solution.
4. Wait for stable reading.
5. Send single-point or two-point calibration command.

The UI displays EC calibration values in `uS/cm`, matching many calibration solution labels.

---

# Temperature Calibration

The RTD probe usually needs less frequent calibration.

Possible reference points:

- Ice water near 0 C
- Boiling water near 100 C, adjusted for elevation
- A trusted thermometer in the same solution

Temperature matters because pH and EC are temperature-compensated.

---

# Pump Calibration

Pump calibration connects commanded volume to actual volume.

Procedure:

1. Prime tubing.
2. Dispense a known test amount.
3. Collect output in a graduated cylinder.
4. Enter the measured amount.
5. Repeat if needed.

Example:

- Commanded: 10 mL
- Measured: 9.4 mL
- Calibrate pump to 9.4 mL

---

# Calibration Records

Students should record:

- Date and time.
- Device ID or hostname.
- Probe or pump calibrated.
- Calibration standards used.
- Pre-calibration reading.
- Post-calibration reading.
- Person performing calibration.
- Notes about stability, bubbles, fouling, or tubing condition.

Calibration is experimental metadata.

---

# Data Collection

Useful data fields:

- Timestamp
- pH
- EC
- Temperature
- Valid flags
- Alarm states
- Pump dose events
- Manual versus automatic action
- Setpoints
- Control gains
- Reservoir volume
- Crop stage

Readings alone are not enough. Actions and context matter.

---

# Sampling Rate

Think about the process time scale.

Fast sampling:

- Captures transient changes.
- Produces more data.
- May show mixing noise.

Slow sampling:

- Easier to analyze.
- May miss important events.

For hydroponic nutrient solution, chemistry usually changes more slowly than the electronics can measure.

---

# Data Quality Checks

Before analysis, check:

- Missing values.
- Invalid flags.
- Sudden impossible jumps.
- Sensor drift.
- Calibration events.
- Pump events.
- Solution changes.
- Probe cleaning events.

Do not treat every number as equally trustworthy.

---

# Basic Analysis

Useful plots:

- pH versus time.
- EC versus time.
- Temperature versus time.
- Dose events overlaid on readings.
- Error from setpoint versus time.

Useful summary metrics:

- Time within target band.
- Number of alarms.
- Dose count.
- Total dose volume.
- Recovery time after disturbance.

---

# Interpreting pH Response

After acid dosing:

- pH should decrease after mixing.
- Response may be delayed.
- Overshoot means dose may be too large.
- Slow correction may mean dose is too small or interval is too long.

If pH rises over time, the system may need periodic acid dosing.

If pH falls too far, automatic recovery is not available in this configuration.

---

# Interpreting EC Response

After nutrient dosing:

- EC should increase after mixing.
- Pumps 2 and 3 dose equally.
- Unequal tubing or calibration errors can create nutrient imbalance.

If EC keeps falling:

- Plants may be consuming nutrients.
- Reservoir volume may be changing.
- Dilution may be occurring.
- The dose may be too small.

---

# Testing Philosophy

Test one thing at a time.

Start with low-risk tests:

- Web UI connects.
- Serial console works.
- Sensor readings update.
- Pumps respond with water only.
- Calibration commands return expected responses.

Then test integrated behavior:

- Manual dosing changes readings.
- Autonomous mode queues expected commands.
- Lock behavior blocks public control.

---

# Sensor Test Plan

Questions:

- Does each sensor respond?
- Are readings stable in a known solution?
- Are invalid readings detected?
- Does temperature compensation run?

Test examples:

- Place pH probe in pH 7 buffer.
- Place EC probe in standard solution.
- Warm or cool solution slightly and observe RTD response.
- Remove probe briefly and observe invalid or suspicious behavior.

---

# Pump Test Plan

Use water before chemicals.

Questions:

- Does each pump run?
- Is tubing connected to the correct reservoir?
- Does positive volume pump forward?
- Does stop command work?
- Is measured volume close to commanded volume?

Record actual volume from repeated tests.

---

# Control Test Plan

Use conservative settings and small changes.

Questions:

- Does auto mode remain off by default?
- Does pH dosing occur only when pH is above setpoint?
- Does EC dosing occur only when EC is below setpoint?
- Are doses clamped?
- Does the controller wait between doses?
- Does the queue drain correctly?

Test with water or low-risk solutions first.

---

# Access Test Plan

Questions:

- Is public control locked on boot?
- Can tailnet/admin unlock public control?
- Does public control stop working after relock?
- Are pump and calibration actions logged?
- Is OTA restricted to tailnet/admin access?

Access control is part of system safety.

---

# Common Failure Modes

Sensors:

- Dirty probe
- Air bubbles
- Dry probe
- Bad calibration
- Loose I2C connection

Pumps:

- Empty chemical container
- Reversed tubing
- Air in line
- Worn tubing
- Miscalibration

Software:

- Wrong settings
- Auto mode enabled too early
- Network or access misconfiguration

---

# Student Lab Exercise 1

## Sensor Baseline

1. Open the web dashboard.
2. Record pH, EC, and temperature for 10 minutes.
3. Note reading stability.
4. Identify any invalid or stale readings.
5. Plot each variable versus time.

Discussion:

- Which signal was most stable?
- What might explain noise or drift?

---

# Student Lab Exercise 2

## Pump Calibration

1. Fill pump source with water.
2. Prime tubing.
3. Dispense 10 mL.
4. Measure actual output.
5. Repeat three times.
6. Calibrate the pump if needed.

Discussion:

- How repeatable was the pump?
- How would pump error affect nutrient control?

---

# Student Lab Exercise 3

## Dose Response

1. Start with a known reservoir volume.
2. Record baseline pH and EC.
3. Add a small manual dose.
4. Continue recording until readings stabilize.
5. Plot response over time.

Discussion:

- What was the delay before readings changed?
- Did the final value match expectations?

---

# Student Lab Exercise 4

## Controller Settings

Use recorded dose-response data to reason about:

- Setpoint
- Alarm thresholds
- Dose interval
- `Kp`
- `Ki`

Discussion:

- Why should `Ki` usually start at zero?
- What happens if dose interval is too short?
- What happens if `Kp` is too high?

---

# Key Takeaways

- CEA control starts with reliable measurement.
- Calibration is part of the experiment, not a side task.
- The controller separates sensing, command queuing, and actuation to protect I2C communication.
- pH and EC control are one-directional in this system.
- Dose-and-wait control respects mixing time.
- Good data analysis includes actions, settings, and calibration history.
- Test safely before enabling autonomous dosing.

---

# Discussion Questions

- What variables are controlled directly, and what variables are only inferred?
- How would reservoir volume affect dosing response?
- What would you change if the system had both acid and base pumps?
- How could this controller be extended to log data automatically?
- What evidence would convince you that autonomous control is working well?
