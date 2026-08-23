# MhaiBot Servo Human-Like Motion Checkpoint

Date: 2026-08-23

## Summary

Human-like manual neck motion on the ESP32-S3 -> ESP32-C3 servo link is validated
on bench hardware.

## Implementation Scope

- Manual MCP tool: `self.neck.move`
- Supported actions: `left`, `right`, `up`, `down`, `center`, `shake`, `nod`
- Motion style: smooth stepped movement, brief hold, smooth return to center
- Idle/random automatic motion: not enabled

## Validation Evidence

Software validation:

- Unit tests: PASS
  - Command: `python -m unittest scripts.tests.test_mhaibot_servo_integration_contracts scripts.tests.test_mhaibot_behavior_model`
  - Result: 5 tests run, 1 skipped
- Whitespace check: PASS
  - Command: `git diff --check`
  - Result: no blocking issues; LF/CRLF warnings only
- ESP32-S3 build: PASS
  - Command: `idf.py build`
  - Result: `xiaozhi.bin` generated successfully
- ESP32-S3 flash: PASS
  - Command: `idf.py -p COM4 flash`
  - Result: `Hash of data verified`

Hardware validation:

- User-confirmed result: PASS
- Smooth `left`, `right`, `up`, and `down` neck commands passed.
- Smooth `shake` and `nod` gestures passed.
- C3 monitor showed multi-step `[uart] move ...` command sequences from S3.
- No stop condition was reported: no mechanical hit, hard jitter, C3 reset,
  S3 reset, display power sag, or rapid servo heating was reported.

## Assembly Gate

Status: Ready for loose mechanical mounting.

Before final assembly:

1. Mount servos loosely with the neck centered.
2. Re-test `left/right/up/down/shake/nod`.
3. Stop immediately if a servo hits the frame, jitters hard, resets a board,
   pulls wiring, or heats quickly.
4. Only tighten the mount after the mounted motion test passes.

## Remaining Risks

- Direction labels may need inversion after physical mounting.
- Mounted wiring may change the safe travel range.
- Repeated rapid voice commands still need rate limiting and collision guards.
