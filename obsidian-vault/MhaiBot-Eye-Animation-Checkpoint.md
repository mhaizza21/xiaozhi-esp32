# Mhaibot Eye Animation — Slice 0–5 Checkpoint

## Current status

Architecture:
COMPLETE / FROZEN

Implementation:
Slices 0–5 complete

Next:
Slice 6 — EmotionController

Current branch:
`feature/mhaibot-eye-animation-framework`

Current HEAD:
`18d1754`

## Slice history

### Slice 0

- EyeFrame
- FaceV2Pose adapter
- board-local architecture baseline

Commit: `82b5b0c`

### Slice 1

- LVGLEyeRenderer
- LVGL eye mutation extracted
- legacy ResolveRenderedPose remains pixel authority

Commit: `e8ea867`

### Slice 2

- EyeAnimator pass-through
- EyeIntent
- EyeIntentMailbox
- latest-wins mailbox skeleton

Commit: `d74d8cb`

### Slice 3

- EyeActivityAdapter
- DeviceState / emotion mapping tables
- shadow-publish begins
- no pixel cutover

Commit: `addeee0`

### Slice 4

- BlinkController
- shared blink post-compose
- deterministic seeded timing

Commit: `e6d2b8e`

**Important correction:** Legacy does have a scoped pseudo-blink in `GroggyPose()` during
`kGroggyWake`. Slice 4 adds a general `BlinkController` but deliberately does not migrate/delete
`GroggyPose` yet.

### Slice 5

- IdleController
- idle gaze post-compose
- composition order: canonical EyeFrame → idle gaze → blink openness → renderer

Commit: `18d1754`

## Architecture state

- Implementation remains board-local.
- EyeFrame is stereo.
- Openness is not stored in EyeFrame.
- LVGL mutation belongs to LVGLEyeRenderer.
- Mailbox is mutex-protected latest-wins.
- Legacy ResolveRenderedPose / InterpolatePose remain pixel authority.
- Mailbox/new pipeline is not pixel-authoritative yet.
- Canonical parity point is before idle/blink post-compose.
- Pet/startle/groggy migration is still pending.
- Legacy deletion is prohibited until later parity/cutover gates pass.

## Validation state

- Freenove ESP32-S3 firmware builds PASS through Slice 5.
- ESP-IDF v6.0.2.
- `git diff --check` clean at Slice 5 checkpoint.
- Working tree clean at `18d1754`.
- Host C++ sources compile with strict warnings.
- Native host runtime tests are NOT EXECUTED because this environment currently lacks a
  Windows-native C++ compiler. These runtime tests are not described as PASS.

## Known risks / follow-up

1. `GroggyPose` pseudo-blink and an already-running general blink can briefly overlap in a
   narrow transition window. Carry forward to Slice 7/8.
2. Idle gaze 20px/25px scale is provisional visual tuning, not an architecture contract.
   Requires later hardware validation.
3. Idle enablement currently relies on a temporary legacy Emotion allowlist. Coordinator in
   Slice 7 should become the policy owner.
4. Physical hardware validation remains pending for visual timing/feel.

## Next implementation

Slice 6: EmotionController

(Not implemented as part of this checkpoint.)

## Links

- Architecture docs: `docs/architecture/eye-animation/` in the `xiaozhi-esp32` repo
  (`feature/mhaibot-eye-animation-framework` branch).
- GitHub remote: `https://github.com/mhaizza21/xiaozhi-esp32` — branch
  `feature/mhaibot-eye-animation-framework` pushed, HEAD `18d1754`, no PR opened yet.
