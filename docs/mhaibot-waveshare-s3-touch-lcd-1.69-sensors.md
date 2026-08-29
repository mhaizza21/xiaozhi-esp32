# Mhaibot Waveshare ESP32-S3-Touch-LCD-1.69 Sensor Support

This document records the first production-oriented software slice for the Waveshare ESP32-S3-Touch-LCD-1.69 board in Mhaibot.

## Scope

Implemented:

- A dedicated board identity: `esp32-s3-touch-lcd-1.69`
- ST7789V2 LCD initialization for the 240x280 display
- CST816T touch initialization through the existing CST816S-compatible ESP-IDF LCD touch driver
- A `MhaibotSensorHub` module for onboard sensor diagnostics
- Read-only MCP tools for sensor status, QMI8658 motion samples, PCF85063 RTC time, and touch bring-up state
- Architecture placeholders for PIR and camera without assigning GPIOs

Not implemented:

- Hardware flashing
- Physical touch/IMU/RTC validation
- PIR GPIO assignment
- Camera module selection or pin mapping
- Behavior automation such as fall detection, servo protection, or RTC-based sleep/wake policy

## Architecture

The board owns hardware-specific initialization and pin assignments. Shared sensor readout lives in `main/sensors/mhaibot_sensor_hub.*` so future Mhaibot boards can reuse the diagnostic boundary without depending on this board class.

The sensor hub intentionally exposes read-only snapshots. It does not mutate `Application` state, does not touch audio tasks, and does not create a background polling loop in this slice.

## MCP Tools

| Tool | Side effect | Notes |
|---|---:|---|
| `self.sensors.get_status` | No | Reports bring-up status and future capability placeholders |
| `self.sensors.get_motion` | No | Reads one QMI8658 sample on demand |
| `self.sensors.get_rtc_time` | No | Reads PCF85063 time registers and clock integrity |
| `self.sensors.get_touch_last_event` | No | Reports touch driver/LVGL registration state |

## Future PIR

Candidate extension GPIOs must be checked against the selected firmware, the actual wiring, and any planned camera or servo bridge use before assignment. This slice deliberately leaves PIR as an unassigned capability placeholder.

## Future Camera

Camera support should be added only after the exact camera module and pin map are chosen. The project already treats camera as optional through board capabilities, so the future implementation should follow that boundary.

## Validation Checklist

Software validation:

1. `python scripts/build.py --list-boards`
2. `python scripts/build.py waveshare/esp32-s3-touch-lcd-1.69 --name esp32-s3-touch-lcd-1.69`

Hardware validation, not done in this slice:

1. Flash only after confirming the correct COM port and board.
2. Confirm LCD boot screen appears.
3. Confirm touch events work in LVGL.
4. Call `self.sensors.get_motion` while tilting the board and confirm accelerometer values change.
5. Call `self.sensors.get_rtc_time` and confirm time registers are valid or intentionally unsynced.
6. Confirm power button and battery behavior separately.
