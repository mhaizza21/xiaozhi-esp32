# MhaiBot Servo Integration Runbook

## Current Status

- ESP32-S3 firmware has a behavior intent model for face and future neck motion.
- ESP32-S3 behavior intent still sends a center-only UART validation command:
  `move 1500 1500`.
- No idle/random automatic neck movement is enabled in this slice.
- Manual MCP tool `self.neck.move` can send bounded human-like validation gestures:
  `left`, `right`, `up`, `down`, `center`, `shake`, and `nod`.
- Manual commands move smoothly, hold briefly, then return center smoothly.

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
x 1350
x 1500
x 1650
x 1500
```

11. Test Y gently:

```text
y 1400
y 1500
y 1600
y 1500
```

12. Test combined motion:

```text
move 1350 1500
move 1500 1500
move 1650 1500
move 1500 1400
move 1500 1500
move 1500 1600
move 1500 1500
```

13. Keep the ESP32-C3 monitor open.
14. Flash and monitor the ESP32-S3 firmware.
15. Confirm the ESP32-C3 monitor receives `[uart] move 1500 1500`.
16. Ask the S3 voice assistant for a small neck movement, then confirm C3
    receives the matching safe command:

```text
left   -> move 1350 1500
right  -> move 1650 1500
up     -> move 1500 1400
down   -> move 1500 1600
center -> move 1500 1500
shake  -> smooth left/right/left/center sequence
nod    -> smooth up/down/up/center sequence
```

The C3 monitor should show several `[uart] move ...` lines for one voice
command because the S3 motion layer sends small steps instead of jumping to the
target pulse width in one command.

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

Expected S3 log pattern after flashing the current S3 firmware:

```text
I (...) MhaiBotServoUart: Servo UART bridge is in human-like manual motion validation mode
I (...) MhaiBotServoUart: Servo UART center-only mode enabled: TX=GPIO43 RX=GPIO44 baud=115200
I (...) MhaiBotServoUart: Center-only UART command sent for motion=<mode>: move 1500 1500
I (...) MCP: Add tool: self.neck.move
I (...) MhaiBotServoUart: Servo UART command sent for <action>: move <x> <y>
I (...) MhaiBotServoUart: Skipping behavior center command while manual neck motion is active
```

Expected C3 monitor pattern:

```text
[uart] move 1500 1500
Servo X (GPIO3) pulse: 1500 us
Servo Y (GPIO10) pulse: 1500 us
OK
```

PASS criteria:

- Build passes.
- S3 boots normally.
- Display face still works.
- Wake word still works.
- Conversation close does not self-trigger.
- ESP32-C3 monitor shows `[uart] move 1500 1500`.
- ESP32-C3 monitor shows the requested manual neck command for
  `left/right/up/down/center/shake/nod`.
- Manual commands move smoothly, hold briefly, then return center smoothly.
- Servos only move within the bounded validation range.
- No idle/random automatic neck movement is enabled in this slice.

## Hardware Evidence

### 2026-08-24 S3 Command Routing Retest Without Servos Connected

Status: PARTIAL / LOG-ONLY

Context:

- User clarified the servos were not connected during this retest.
- Therefore, this run must not be counted as physical servo validation.
- Log file:
  `logs/s3-wake-retetest-20260824-030641.log`

Observed S3 command-routing evidence:

```text
Application: >> พยักษหน้า
Application: << % self.neck.move...
MhaiBotServoUart: Servo UART command sent for nod-up: move 1500 1450
MhaiBotServoUart: Servo UART command sent for nod-down: move 1500 1600
MhaiBotServoUart: Servo UART command sent for nod-center: move 1500 1500

Application: >> หันไปทางซ้าย
Application: << % self.neck.move...
MhaiBotServoUart: Servo UART command sent for look-left: move 1350 1500
MhaiBotServoUart: Servo UART command sent for soft-return-center: move 1500 1500
```

Interpretation:

- S3 MCP/manual neck command routing is still present.
- S3 generated bounded UART `move ...` commands for `nod` and `look-left`.
- Physical servo movement is NOT TESTED because the servos were disconnected.

Required follow-up when hardware is connected:

1. Connect C3 and servos using the wiring table above.
2. Open the C3 monitor on COM5.
3. Re-run `Hi ESP`, then ask for:
   - `พยักหน้า`
   - `หันไปทางซ้าย`
   - `หันไปทางขวา`
   - `ส่ายหน้า`
4. Confirm both:
   - S3 monitor emits bounded `MhaiBotServoUart` commands.
   - C3 monitor receives `[uart] move ...` and the physical servos move safely.

### 2026-08-23 Human-Like Manual Neck Motion

Status: PASS

Software validation evidence:

- `python -m unittest scripts.tests.test_mhaibot_servo_integration_contracts scripts.tests.test_mhaibot_behavior_model`
  passed: 5 tests run, 1 skipped.
- `git diff --check` passed with only Windows LF/CRLF warnings.
- ESP32-S3 `idf.py build` passed.
- ESP32-S3 `idf.py -p COM4 flash` passed with `Hash of data verified`.

Hardware observation evidence:

- User confirmed the human-like servo motion test passed after flashing the S3
  firmware.
- Smooth `left`, `right`, `up`, and `down` manual neck commands passed.
- Smooth `shake` and `nod` manual neck gestures passed.
- C3 monitor showed multi-step `[uart] move ...` command sequences from the S3
  motion layer.
- No stop condition was reported during this test: no frame hit, hard jitter,
  C3 reset, S3 reset, display power sag, or rapid servo heating was reported.

Scope of this PASS:

- Bench hardware validation only.
- The robot body has not been fully assembled around the pan/tilt mechanism yet.
- Re-test is required after mounting the servos into the physical body.

## Next Slice

Only after the above passes on hardware:

1. Mount the servos into the robot body loosely, starting from `center`.
2. Re-test `left/right/up/down/shake/nod` after mounting.
3. Tune or invert X/Y direction names if physical motion does not match labels.
4. Add rate limiting and collision guards for repeated voice commands.
5. Re-run wake/sleep validation after enabling broader motion.
