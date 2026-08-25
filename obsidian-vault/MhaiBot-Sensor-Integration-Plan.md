# MhaiBot Sensor Integration Plan

Date: 2026-08-23

## Goal

Add simple environment awareness to MhaiBot without disturbing the validated S3-to-C3 neck servo setup.

Analogy: the current robot already has a face, voice, and neck. Sensors are extra senses. We should add one sense at a time, like giving the robot a simple motion detector first, then a near-object detector, then temperature/humidity later.

## Current Baseline

- S3 display/audio/wake/servo behavior is already running.
- S3 talks to the C3 servo controller over UART.
- C3 owns the servos:
  - X pan servo: C3 GPIO3
  - Y tilt servo: C3 GPIO10
- C3 should stay servo-only for now.
- New sensors should connect to the S3 first, not the C3.

## Source Evidence From esp32-learning

Path checked: `C:\Users\Mhaiz\Projects\esp32-learning`

Important evidence boundary:

- The PIR result below is from a different ESP32 board used in the
  `esp32-learning` project. It is not physical evidence for the current
  Freenove ESP32-S3 MhaiBot board.
- Treat it as reference logic only: it proves the lesson wiring/code worked on
  that other board, not that the current S3 + current PIR wiring works.

| Lesson | Sensor | Old board pin | Old-board result | Reuse decision |
|---|---|---:|---|---|
| `05-pir-motion` | PIR motion sensor | GPIO27 | PASS on different ESP32 board | Reuse logic only; retest current S3 on IO14 |
| `06-ir-sensor` | IR obstacle sensor | GPIO26 | SUCCESS | Reuse active-low logic, choose new S3 pin |
| `04-oled-display` | DHT11 | GPIO4 | PASS | Reuse later, but do not use S3 GPIO4 |
| `04-oled-display` | SSD1306 OLED | SDA21/SCL22 | PASS | Do not add now; S3 already has the main LCD |

Important: these old GPIO numbers are for an ESP32 DevKit V1 learning board. They are not automatically safe for the Freenove ESP32-S3 display board.

## Freenove S3 Pins Already Reserved

Avoid these pins unless the board config is changed deliberately:

| Pins | Reserved for |
|---|---|
| GPIO1, GPIO4, GPIO5, GPIO6, GPIO7, GPIO8 | Audio codec / I2S / PA |
| GPIO15, GPIO16 | Audio codec I2C |
| GPIO10, GPIO11, GPIO12, GPIO13, GPIO45, GPIO46 | LCD display |
| GPIO0 | Boot button |
| GPIO2 | MhaiBot emotion button |
| GPIO42 | Built-in LED |
| GPIO43, GPIO44 | S3-to-C3 servo UART |
| GPIO19, GPIO20 | Avoid for now; commonly USB/JTAG related on S3 boards |
| GPIO9 | Avoid until verified; code uses ADC1 channel 8 for battery monitor |

## Physical Header Evidence

User photo dated 2026-08-23 shows these accessible connector labels on the Freenove ESP32-S3 display board:

| Connector label | Decision |
|---|---|
| IO2 | Avoid; already used as the MhaiBot emotion button |
| IO3 | Keep as backup only |
| IO14 | Best first candidate for PIR OUT |
| IO21 | Backup candidate if IO14 is not usable |
| 3.3V / GND / IO15(SCL) / IO16(SDA) | Reserved I2C/audio codec area; do not use for first PIR test |
| UART RXD/TXD/GND/5V | Do not use for sensors |

## Recommended Sensor Order

1. PIR motion sensor first.
2. IR obstacle sensor second.
3. DHT11 third, only if the physical robot needs temperature/humidity.
4. Do not add the small OLED to MhaiBot now.

Reason: PIR is the safest first integration because it is a simple digital input. It can tell MhaiBot "someone is nearby" without requiring distance math or timing-sensitive protocols.

## Proposed Wiring: First Sensor Only

First target: PIR motion sensor, log-only firmware.

| PIR Sensor | MhaiBot S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| OUT | IO14 / GPIO14 |

Backup pin if IO14 is not available or physically awkward:

| PIR Sensor | MhaiBot S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| OUT | IO21 / GPIO21 |

Do not connect both IO14 and IO21. Pick one signal pin only.

## Proposed Wiring: Second Sensor Later

Second target: IR obstacle sensor, after PIR passes.

| IR Sensor | MhaiBot S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| OUT | IO21 / GPIO21, only after PIR on IO14 passes |

IR module logic from `esp32-learning`:

- `LOW` means object detected.
- `HIGH` means no object.

## Safety Rules

- Power off before changing wires.
- Do not move sensor wires while USB or external servo 5 V is powered.
- Use 3.3 V for these sensor signal modules unless the module documentation says otherwise.
- Never send 5 V signal into an ESP32 GPIO.
- Sensor GND must share the same GND as S3.
- Keep external servo 5 V wiring unchanged.
- Test one new sensor at a time.

## First Firmware Slice

Status: implemented as software/log-only preparation on 2026-08-23.

Current-board PIR evidence status:

- PARTIAL / LOG-ONLY on the current Freenove ESP32-S3 board.
- Confirmed current firmware reads IO14 and prints periodic status:
  - `PIR IO14 status: level=0 clear`
- NOT YET PHYSICALLY PASSED on the current S3 board because motion has not
  produced:
  - `PIR IO14 changed: level=1 motion detected`
- The earlier `05-pir-motion` PASS belongs to another ESP32 board and must not
  be counted as current S3 hardware validation.

Scope:

- Add one PIR input pin on S3: IO14 / GPIO14.
- Configure GPIO14 as input with internal pull-down.
- Log periodic raw status plus state changes:
  - `PIR IO14 status: level=0 clear`
  - `PIR IO14 changed: level=1 motion detected`
  - `PIR IO14 changed: level=0 clear`
- No servo movement.
- No automatic speech.
- No wake behavior changes.
- No C3 firmware changes.

Acceptance criteria:

- `idf.py build` passes.
- S3 boots normally.
- LCD still works.
- Audio/wake still works.
- C3 servo UART still works.
- Serial monitor shows PIR state changes when motion is detected.

Stop immediately if:

- S3 display stays black after boot.
- Wake/audio stops working.
- Servo jitters unexpectedly.
- Board resets when sensor is connected.
- Sensor output is 5 V.

## Next Step

Keep IO14 as the first PIR OUT pin in the log-only firmware slice. Do not wire
the IR sensor yet. The next physical goal is to make the current S3 monitor
show `PIR IO14 changed: level=1 motion detected` from the currently connected
PIR sensor.
