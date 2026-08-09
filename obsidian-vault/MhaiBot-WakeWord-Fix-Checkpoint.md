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

Notes:
- Threshold 0.4 is currently hardware-validated.
- Keep as current working value.
- Future tuning may evaluate false wake rate and detection range.

Scope:
- Separate from Slice 11B eye animation/rendering/servo work.
