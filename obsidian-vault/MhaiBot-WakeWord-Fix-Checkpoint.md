# MhaiBot WakeWord Fix Checkpoint

Status: VALIDATED

Architecture:
- Option C selected
- PowerSaveTimer(-1) restored for Freenove ESP32-S3 Display 2.8 LCD
- WakeNet remains active during sleep face
- Added 1500ms wake-word cooldown after audio channel close

Wake word:
- Model: wn9_hiesp
- Wake word: "Hi ESP"
- Threshold: 0.4

Validation:
PASS
- Hi ESP wakes device from real sleep face
- Wake path confirmed by logs:
  Application: Wake word detected: Hi,ESP
  PowerSaveTimer: Exiting power save mode
- Multiple conversation close tests completed
- No self-trigger loop observed

2026-08-21 validation update:
PASS
- Firmware was built, flashed, and monitored on Freenove ESP32-S3 Display 2.8 LCD via COM4.
- Basic runtime passed after flash: boot, Wi-Fi/MQTT session, wake word, AI conversation,
  and face emotion updates were observed without panic, brownout, watchdog, or reset loop.
- Conversation-close loop guard passed across five clean close events. Observed transitions
  wound down cleanly (`speaking -> listening -> idle`) without a self-triggering wake loop.
- Real sleep voice wake passed. The device had entered the sleep face at approximately
  1197s and was still asleep approximately 265s later when the wake word was spoken.
- Wake path was confirmed by log evidence, not touch:
  ```text
  [1462.90s] Application: Wake word detected: Hi,ESP (state: 3)
  [1463.31s] Backlight: Set brightness to 80
  [1463.31s] PowerSaveTimer: Exiting power save mode
  [1464.33s] << สวัสดีครับ! ผมเป็นเพื่อนคู่คิดของโปรเจกต์ AI Space Colony Simulation ครับ วันนี้มาช่วยกันทำอะไรดีครับ?
  ```
- A later auto-timeout goodbye after approximately 90s of silence also closed cleanly,
  including the close immediately after the real sleep-wake test.

Notes:
- Threshold 0.4 is currently hardware-validated.
- Keep as current working value.
- Future tuning may evaluate false wake rate and detection range.

Scope:
- Separate from Slice 11B eye animation/rendering/servo work.
