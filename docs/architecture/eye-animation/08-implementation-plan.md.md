# 08. Implementation Plan

**Status:** Draft  
**Canonical sequence:** `06-rendering-pipeline.md` §11  
**Companion roadmap:** `04-eye-animation-roadmap.md` (same order, shorter table)

## Goal

Implement the Eye Animation Framework by extracting from
`MhaiBotFaceV2` in small, reviewable phases. This plan and `04` describe
the **same** sequence. Prefer board-local changes only.

------------------------------------------------------------------------

## Phase 0 — Documentation

- [x] Architecture documents present under `docs/architecture/eye-animation/`
- [x] ADR records canonical `05`/`06` decisions
- [x] API draft with mailbox / ownership (`02`)
- [x] Rendering pipeline (`06`)
- [x] State machine (`05`)
- [x] Behavior spec (`07`)
- [x] Remaining docs reconciled to `05`/`06`/`07`
- [x] Open decisions closed: ADR-002…006 (mapping, ownership, openness,
  mailbox, shared-exit)

Deliverable: internally consistent design pack ready for extract work.

------------------------------------------------------------------------

## Phase 1 — Inspect Existing Display Path

Tasks:

- Trace `MhaiBotDisplay::SetEmotion`, face show/hide, and interaction
  model sleep / pet / startle entry points.
- Confirm how `DeviceState` and emotion strings reach the face today.
- Note timer ownership (`lv_timer` in `MhaiBotFaceV2`) and LVGL thread
  assumptions.

Deliverable: short notes in the PR description (no behavior change).

------------------------------------------------------------------------

## Phase 2 — Identify LVGL Eye Objects and Ownership

Tasks:

- Document `root_`, `left_eye_`, `right_eye_`, `sleep_label_` ownership.
- Confirm objects are created once and updated in place (already true).
- Apply **ADR-003** ownership: `sleep_label` + Show/Hide stay on the face
  shell / `MhaiBotDisplay`; backlight/panel power stay on the Freenove
  board; renderer updates only the two eye objects.

Deliverable: ownership map used by later PRs; no visible change.

------------------------------------------------------------------------

## Phase 3 — Introduce `EyeFrame` Without Visible Change

Tasks:

- Add board-local `EyeGeometry` / `EyeFrame` types matching `06`.
- Add Pose → `EyeFrame` adapter from current `MhaiBotFaceV2::Pose`.
- Keep applying geometry through the existing path or a no-op dual path
  that produces identical pixels.

Deliverable: compiles; **no functional / visible change**.

------------------------------------------------------------------------

## Phase 4 — Renderer Adapter

Tasks:

- Implement `LVGLEyeRenderer` that updates the existing left/right eye
  objects from `EyeFrame`.
- Clamp / reject non-finite values before LVGL (`06` §5 / §10).
- Route the face tick through Render(frame) while frame still mirrors
  legacy pose output.

Deliverable: stable LVGL update path driven by `EyeFrame`; behavior
unchanged.

------------------------------------------------------------------------

## Phase 5 — `EyeAnimator` with Neutral Static Output

Tasks:

- Introduce `EyeAnimator::Update(delta_ms)` with no LVGL includes.
- Initially emit static / neutral frames equivalent to current neutral
  pose (or pass-through of adapted legacy targets).
- Wire coordinator shell + intent mailbox skeleton (**ADR-005**:
  mutex latest-wins `EyeIntentMailbox`) if needed for the tick path.

Deliverable: animator owns frame production; still visually equivalent.

------------------------------------------------------------------------

## Phase 6 — Blink

Tasks:

- Add `BlinkController` with timing ranges from `07` §5.
- Apply openness **multiplier** in composition order (`07` §9,
  **ADR-004**); do not add openness to `EyeFrame`.
- Honor suppression during Sleeping / Waking / owning transitions
  (`05` §8).

Deliverable: auto-blink and manual/double-blink paths validated on
hardware when possible; no watchdog / jump regressions.

------------------------------------------------------------------------

## Phase 7 — Idle Movement

Tasks:

- Add `IdleController` event-driven micro gaze / glance (`07` §6).
- Disable or reduce idle under Listening / Thinking / Speaking per
  `07` §7.
- Seeded / event-based randomness only (no per-frame random).

Deliverable: subtle idle life without competing with interaction poses.

------------------------------------------------------------------------

## Phase 8 — Emotion Mapping

Tasks:

- Add `EmotionController` for Neutral / Happy / Sad / Angry / Surprised /
  Focused / Sleepy (`05` / `07`).
- Complete `DeviceState` + `SetEmotion` → `EyeIntent` adapter (`02`).
- Map activity behaviors for Idle / Listening / Thinking / Speaking /
  Sleeping / Waking / Error.
- Preserve priority rules (`05` §7).

Deliverable: emotion and activity expression driven by structured
intent; legacy face emotion enum usage reduced to the adapter edge.

------------------------------------------------------------------------

## Phase 9 — Remove Legacy Animation Logic

Tasks:

- After hardware validation checklist in `07` §12, delete replaced
  pose/transient interpolation paths inside `MhaiBotFaceV2` that the
  animator now owns.
- Keep board shell responsibilities (Show/Hide, sleep label if still
  separate, display integration).
- Do **not** move code into shared `main/display/` in this phase.

Deliverable: single animation path; legacy duplication gone; Freenove
behavior still passes validation.

------------------------------------------------------------------------

## Validation (continuous; hard gate before Phase 9)

Checklist (see also `07` §12 and `06` §12):

- Visual review of emotions, blink, idle, sleep/wake, error interrupt
- No one-frame jumps, clipping, overlap, or flicker
- No watchdog reset; no unexpected heap growth
- Acceptable frame interval on SPI LCD
- Existing UI/status behavior remains functional
- Host-side unit tests for geometry composition where practical

------------------------------------------------------------------------

## Explicit Non-Goals

- Render queue / dirty-region scheduler phase
- Separate global Transition Engine or Animation Scheduler
- Greenfield tree under `main/display/eye/`
- Parallel application state machine replacing `DeviceState`
- Warning emotion (not in `05` enums); use `Error` / existing alerts
