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

## Sign-off decisions

### Slice 10 Decision — Pet Ramp-In Behavior

Status:
APPROVED for Slice 11 cutover.

Accepted difference:
The first 300ms Pet entry animation is not pixel-identical between legacy and shadow paths.

Reason:
Legacy implementation uses recursive interpolation:

`current_pose_ -> PettingPose()`

with `current_pose_` updated every tick.

Shadow implementation uses deterministic anchor interpolation:

captured `pet_anchor_ -> PettingPose()`

Impact:
- Difference is limited to the ramp-in phase (<300ms).
- Peak measured divergence is approximately 16px.
- Endpoint and hold phase match.
- No divergence after transition completes.
- Pet sway/bob/opacity phase differences remain covered by Slice 9 exclusion rules.

Decision:
Accept this as an intentional behavior difference.

Do NOT:
- redesign Coordinator interpolation
- reintroduce recursive state
- modify Slice 8 transient composition
- modify parity harness tolerances

Recorded at HEAD: `6fbff30` (Slice 10 hotfix — ShowNotification dual-feed).

### Slice 11 Decision 1 — Startle during Thinking/Listening/Speaking

Status:
ACCEPTED

Reason:
Legacy stores Thinking/Listening/Speaking geometry inside `ResolveBasePose()`. The shadow
architecture intentionally separates:
- `EmotionController` = emotion geometry
- `EyeActivity` = activity adjustment

Reintroducing legacy-only activity-baked geometry into Startle would violate the current
ownership model.

Impact:
Startle parity remains guaranteed only for representable emotion states: Neutral, Robot2,
Happy, Confident/Focused, Relaxed/Sleepy.

No code change required.

### Slice 11 Decision 2 — Same-emotion restart during active transition/transient

Status:
ACCEPTED

Reason:
Legacy receives direct emotion events and restarts transitions unconditionally. The mailbox
`EyeIntent` model is latest-state based and does not preserve repeated-event identity. Adding
event semantics is outside Slice 11 scope.

Impact:
Repeated same-emotion calls during active transition/transient may differ between legacy and
shadow.

No code change required.

Recorded at HEAD: `6fbff30`. Both decisions accepted for Slice 11 cutover — all previously open
Slice 10 readiness-review sign-off gaps are now closed. Slice 11 implementation not yet
authorized.

## Hardware validation results

### GPIO2 Emotion Cycle Button — Validation

Status:
PASS

Commit:
`eff8062` (feat(mhaibot): add GPIO2 emotion cycle button)

Feature:
Physical GPIO2 button (`INPUT_PULLUP`, 50ms debounce) cycles emotion on each press:
`happy -> thinking -> confident -> sleepy -> neutral`, then wraps. Gated by the same
`!screen_off_ && !sleeping_face_active_ && !groggy_wake_active_` condition as Pet/Startle
touch detection; each press cancels any active transient before calling `SetEmotion`.

Evidence:
- ESP-IDF build: PASS
- Hardware flash (Freenove ESP32-S3, no NVS erase — Wi-Fi credentials preserved): PASS
- Live monitor: `Emotion button IO2 -> ...` logged 33 times across repeated presses
- Sequence correct on every cycle: `happy -> thinking -> confident -> sleepy -> neutral`,
  wrapping back to `happy` — no skipped, duplicated, or misordered steps
- No panic / watchdog / brownout / reboot during the button test
- User visually confirmed eye changes on the physical display for each press

### Slice 11B — Deterministic Rollback Validation (engineer-only console hook)

Emotion transition mid-lerp rollback: PASS
Evidence: Shadow -> Legacy rollback fired exactly 150 ms into the 300 ms emotion transition. Legacy geometry remained mid-transition and the board remained responsive without panic/reset/watchdog/brownout.

Groggy transient deterministic rollback: PASS
Evidence: engineer-only console hook started Groggy under Shadow and rolled back to Legacy exactly 2500 ms later. Legacy geometry remained in the Groggy pose and the board remained responsive. Two continuous-serial reruns showed no firmware reset.

Normal sleep/screen-off Groggy rollback: NOT EXECUTED
Reason: the deterministic hook called StartGroggyWake() directly and did not exercise the real idle-timeout, screen-off, touch-wake path.

Serial reset note:
The apparent reset during the first run occurred only after reopening the serial port. Continuous-connection reruns did not reset, identifying DTR/RTS auto-reset as test-tool artifact rather than firmware failure.

## Links

- Architecture docs: `docs/architecture/eye-animation/` in the `xiaozhi-esp32` repo
  (`feature/mhaibot-eye-animation-framework` branch).
- GitHub remote: `https://github.com/mhaizza21/xiaozhi-esp32` — branch
  `feature/mhaibot-eye-animation-framework` pushed, HEAD `18d1754`, no PR opened yet.
