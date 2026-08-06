# Eye Animation Implementation Roadmap

**Status:** Draft  
**Canonical sequence:** `06-rendering-pipeline.md` §11  
**Companion plan:** `08-implementation-plan.md` (same phases, more checklist detail)

## Goal

Extract the Eye Animation Framework from the existing Freenove
`MhaiBotFaceV2` face in small, reviewable PRs. There is **exactly one**
implementation order for this directory: the sequence below. Do not use
older Animator-before-renderer or greenfield `main/display/eye/` plans.

## Single Sequence (from `06` §11)

| Phase | Scope | Deliverable |
|---|---|---|
| 0 | Documentation gate | Docs aligned to `05`/`06`/`07` (this pack) |
| 1 | Inspect | Current display, interaction model, LVGL ownership understood |
| 2 | Identify objects | Existing left/right eye objects and tick owner documented |
| 3 | `EyeFrame` | Stereo frame type + Pose→Frame adapter; **no visible change** |
| 4 | Renderer adapter | `LVGLEyeRenderer` drives pre-created eyes from `EyeFrame` |
| 5 | Animator (static) | `EyeAnimator` emits neutral/static frames through the renderer |
| 6 | Blink | `BlinkController` + suppression rules |
| 7 | Idle | `IdleController` micro gaze / glance |
| 8 | Emotion | `EmotionController` + activity/emotion mapping from adapter |
| 9 | Delete legacy | Remove replaced `MhaiBotFaceV2` animation paths **after** hardware validation |

Phases 3–8 should each preserve prior behavior except for the feature
being introduced. Phase 9 is gated on the `07` hardware checklist.

## PR / AI Task Rules

Each PR should:

- Touch only its assigned board-local files under
  `main/boards/freenove-esp32s3-display-2.8-lcd/` (and tests).
- Avoid unrelated refactoring and shared `main/display/` moves.
- Build successfully.
- Preserve existing behavior unless the PR explicitly changes it.
- Prefer extract/adapter over rewrite.
- Follow types from `05`/`06` (`EyeIntent` struct, stereo `EyeFrame`).

## Out of Scope for this roadmap

- Transition Engine, Animation Scheduler, Render Queue
- Flat `EyeIntent` enum / single-eye frame
- Premature shared-module extraction
- Camera gaze, servo sync, network-scripted animations (`07` §13)

## Definition of Done (overall)

- Builds for the Freenove board variant.
- Behavior matches `07` within tuning tolerance on hardware.
- No per-frame allocation; LVGL only from the display-safe context.
- Legacy face animation logic removed only after validation.
- Clear code review; no unrelated file changes.

## Relationship to `08`

`08-implementation-plan.md` expands the same phases into checklists. If
`04` and `08` ever disagree, both must be corrected to match `06` §11.
