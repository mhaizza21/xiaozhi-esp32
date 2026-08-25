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

2026-08-24 retest after wake-word config restore:
PASS with network caveat

Context:
- User requested restoring only the previous wake-word behavior after a test run
  showed the device loading the wrong WakeNet model.
- Source files were not changed for this retest. The firmware was rebuilt and
  flashed from the current tree after restoring the generated wake-word config
  to the validated model.
- S3 port: COM4.
- Log file:
  `logs/s3-wake-retetest-20260824-030641.log`

Build and flash evidence:
- `idf.py build` passed.
- `xiaozhi.bin` size passed partition check:
  `0x2c7600 bytes`, smallest app partition `0x3f0000 bytes`, `29%` free.
- `idf.py -p COM4 flash` passed.
- COM4 was identified by esptool as ESP32-S3.
- Bootloader, partition table, generated assets, and app image all verified with
  `Hash of data verified`.

Runtime wake-word evidence:
```text
I (...) AfeAudioEngine: Model 0: wn9_hiesp
I (...) AFE_CONFIG: Set WakeNet Model: wn9_hiesp
MC Quantized wakenet9: wakeNet9_v1h24_Hi,ESP_3_0.63_0.635
wakeNet9_v1h24_Hi,ESP_3_0.63_0.635 set threshold for 1 word: 0.400000
I (...) AfeAudioEngine: set_wakenet_threshold(0.4000) result: 1
```

Conversation evidence:
```text
I (...) Application: Wake word detected: Hi,ESP (state: 3)
I (...) StateMachine: State: idle -> connecting
I (...) MQTT: Session ID: 2e52b3e3
I (...) StateMachine: State: connecting -> listening
I (...) Application: >> Hi,ESP
I (...) StateMachine: State: listening -> speaking
I (...) Application: << สวัสดีครับ!
I (...) Application: << ผม Nova ผู้ช่วยเสียงของคุณ มีอะไรให้ผมช่วยเรื่อง ESP32 ไหมครับ?
```

Conversation close evidence:
```text
I (...) Application: << ดูเหมือนคุณไม่ได้พูดอะไรนานแล้วครับ ขอตัวลาก่อนนะครับ บ๊ายบายครับ!
I (...) MQTT: Received goodbye message, session_id: 2e52b3e3
I (...) MQTT: Closing audio channel, send_goodbye: 0
I (...) StateMachine: State: listening -> idle
```

Manual neck command routing evidence observed in logs only:
```text
I (...) Application: >> พยักษหน้า
I (...) Application: << % self.neck.move...
I (...) MhaiBotServoUart: Servo UART command sent for nod-up: move 1500 1450
I (...) MhaiBotServoUart: Servo UART command sent for nod-down: move 1500 1600
I (...) MhaiBotServoUart: Servo UART command sent for nod-center: move 1500 1500
I (...) Application: >> หันไปทางซ้าย
I (...) MhaiBotServoUart: Servo UART command sent for look-left: move 1350 1500
I (...) MhaiBotServoUart: Servo UART command sent for soft-return-center: move 1500 1500
```

Servo hardware result:
- NOT TESTED in this retest. User was not with the board and later clarified
  the servos were not connected.
- The UART command lines above only prove S3 command routing, not physical servo
  movement.

Network caveat:
- Wake-word detection continued to work during later retries, but several
  attempts failed before a cloud response:
  ```text
  E (...) MQTT: Failed to receive server hello
  I (...) StateMachine: State: connecting -> idle
  E (...) esp-tls-mbedtls: read error :-0x0050
  I (...) esp_mqtt: MQTT error occurred: ESP_ERR_MBEDTLS_SSL_READ_FAILED
  I (...) MQTT: MQTT disconnected, schedule reconnect in 60 seconds
  I (...) esp_mqtt: MQTT error occurred: ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME
  ```
- Interpretation: these failures are network/server/MQTT path issues after the
  wake word has already been detected. They are not evidence that WakeNet failed.

Current conclusion:
- Wake-word model and threshold are restored to the validated setting:
  `wn9_hiesp`, threshold `0.4`.
- Wake detection on real firmware is PASS.
- At least one conversation path completed and closed cleanly.
- Network/MQTT reliability remains a separate follow-up item.
