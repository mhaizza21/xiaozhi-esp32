# MhaiBot Waveshare ESP32-S3-Touch-LCD-1.69 Sensor Support

Date: 2026-08-29

## Summary

Added the first software slice for the Waveshare ESP32-S3-Touch-LCD-1.69 board in the Mhaibot firmware repo.

Updated with the next layer: a read-only `interaction context` that turns raw sensor reads into advisory posture, motion, touch, and time labels.

## Implemented

- New board profile: `waveshare/esp32-s3-touch-lcd-1.69`
- New firmware identity: `esp32-s3-touch-lcd-1.69`
- LCD bring-up path for ST7789V2 240x280
- CST816T touch bring-up path using the existing CST816S-compatible ESP-IDF touch driver
- Reusable `MhaibotSensorHub` for onboard sensor diagnostics
- Read-only MCP tools:
  - `self.sensors.get_status`
  - `self.sensors.get_motion`
  - `self.sensors.get_rtc_time`
  - `self.sensors.get_touch_last_event`
  - `self.sensors.get_interaction_context`
- Interaction context v1:
  - posture: `upright`, `face_down`, `side_or_strong_tilt`, `lifted_or_freefall`
  - motion event: `stable`, `moving`, `tilted`, `shake_like_motion`, `impact_like_motion`, `low_gravity`
  - suggested face label: `neutral`, `attentive`, `concerned`, `startled`
  - touch freshness from the latest board-level event
  - RTC time bucket: `night`, `morning`, `afternoon`, `evening`
- Future placeholders for PIR and camera without assigning GPIOs

## Not Done

- No firmware flash
- No physical LCD/touch/IMU/RTC validation
- No PIR GPIO assignment
- No camera module or pin mapping
- No servo protection or sensor-driven behavior automation
- No automatic face changes from sensor context yet

## Validation Plan

Build validation:

```sh
python scripts/build.py --list-boards
python scripts/build.py waveshare/esp32-s3-touch-lcd-1.69 --name esp32-s3-touch-lcd-1.69
```

Hardware validation must be done later on the real board after confirming the correct COM port.

After flashing in a later approved hardware session, call `self.sensors.get_interaction_context` while changing the board posture. Expected checks:

- Flat/upright: `posture=upright`, `primary_event=stable` when still
- Tilted sideways: `posture=side_or_strong_tilt`
- Face down: `posture=face_down`
- Shake/move: `primary_event=shake_like_motion` or `moving`

## Evidence Boundary

Build pass means compile-only. Touch, IMU, RTC, interpreted context, battery, buzzer, PIR, camera, and power behavior remain `NOT TESTED` until hardware logs or physical observation are collected.
