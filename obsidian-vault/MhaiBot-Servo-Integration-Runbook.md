# MhaiBot Servo Integration Runbook

## Current Status

- ESP32-S3 firmware has a behavior intent model for face and future neck motion.
- ESP32-S3 firmware logs behavior intent only.
- ESP32-S3 does not send UART bytes to the ESP32-C3 servo controller yet.
- Servo hardware validation is not executed in this slice.

## Hardware Roles

| Part | Role |
|---|---|
| ESP32-S3 Freenove display board | Voice, face, app state, behavior intent |
| ESP32-C3 servo controller | Pan/tilt servo PWM |
| External 5 V supply | Servo power |

## Servo Wiring

| Signal | Connection |
|---|---|
| Servo X signal | ESP32-C3 GPIO3 |
| Servo Y signal | ESP32-C3 GPIO10 |
| Servo red wires | External 5 V positive rail |
| Servo brown/black wires | External 5 V ground rail |
| ESP32-C3 GND | External 5 V ground rail |
| ESP32-S3 GND | ESP32-C3 GND |

X is pan: left/right.

Y is tilt: up/down.

## S3 to C3 UART Wiring

Use the existing TX/RX pins on the ESP32-S3 board.

| ESP32-S3 | ESP32-C3 |
|---|---|
| S3 TX | C3 GPIO20 RX |
| S3 RX | C3 GPIO21 TX |
| S3 GND | C3 GND |

TX must cross to RX. RX must cross to TX.

## Power Safety

- Do not power servos from ESP32 3.3 V.
- Do not power servos from ESP32 5 V during load tests.
- Use external 5 V for servo red wires.
- Share ground between external 5 V, ESP32-C3, and ESP32-S3.
- If servos jitter or reset the boards, stop and add a 470 uF to 1000 uF capacitor
  across the servo 5 V and GND rail near the servos.

## Bench Test Order

1. Disconnect all power.
2. Wire ESP32-C3 to servos.
3. Wire shared ground.
4. Power ESP32-C3 over USB.
5. Open ESP32-C3 monitor.
6. Send `status`.
7. Send `center`.
8. Power the external 5 V servo rail.
9. Send `center` again.
10. Test X gently:

```text
x 1450
x 1500
x 1550
x 1500
```

11. Test Y gently:

```text
y 1470
y 1500
y 1530
y 1500
```

12. Test combined motion:

```text
move 1450 1500
move 1500 1500
move 1550 1500
move 1500 1470
move 1500 1500
move 1500 1530
move 1500 1500
```

## Stop Conditions

Stop immediately if any of these happen:

- Servo hits the mechanical frame.
- Servo jitters hard.
- ESP32-C3 resets.
- ESP32-S3 resets.
- Display flickers from power sag.
- Servo heats quickly.
- Wake-word loop returns after S3 firmware changes.

## S3 Firmware Validation For This Slice

Expected log pattern after flashing the current S3 firmware:

```text
I (...) MhaiBotServoUart: Servo UART bridge disabled; behavior intents are log-only
I (...) MhaiBotServoUart: Log-only intent: motion=ListeningHold neck_milli=(0, 0) servo_output_allowed=false
I (...) MhaiBotServoUart: Log-only intent: motion=SpeakingNod neck_milli=(0, 80) servo_output_allowed=false
I (...) MhaiBotServoUart: Log-only intent: motion=SleepPose neck_milli=(0, 0) servo_output_allowed=false
```

PASS criteria:

- Build passes.
- S3 boots normally.
- Display face still works.
- Wake word still works.
- Conversation close does not self-trigger.
- Logs show behavior intent transitions.
- No servo movement is caused by S3 firmware in this slice.

## Next Slice

Only after the above passes on hardware:

1. Add an explicit S3 UART transport enable flag.
2. Start by sending only `center`.
3. Add rate limiting before sending any `move` command.
4. Re-run wake/sleep validation after enabling real UART output.
