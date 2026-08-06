# 00. Eye Animation Overview

**Status:** Draft  
**Canonical sources:** `05-state-machine.md`, `06-rendering-pipeline.md`,
`07-animation-spec.md`

## Purpose

This directory is the architecture and implementation blueprint for the
MhaiBot Eye Animation Framework.

The goal is to define **how the system is designed** before changing
production face code, and to keep every document describing the **same**
architecture: the model in `05`/`06`/`07`.

The first implementation extracts behavior from the existing Freenove
`MhaiBotFaceV2` face. It does not invent a parallel greenfield tree under
shared display modules.

------------------------------------------------------------------------

## Reading Order

1. `README.md` — index and principles
2. `ADR.md` — decisions and rejected alternatives
3. `01-eye-animation-framework.md` — layered architecture
4. `02-eye-animation-api.md` — public APIs, ownership, mailbox
5. `03-eye-animation-sequence.md` — runtime sequence and timing
6. `04-eye-animation-roadmap.md` — single implementation roadmap
7. `05-state-machine.md` — canonical activity / emotion / transient model
8. `06-rendering-pipeline.md` — canonical pipeline and data model
9. `07-animation-spec.md` — canonical visible behavior
10. `08-implementation-plan.md` — phased plan (same order as `04` / `06` §11)

------------------------------------------------------------------------

## Binding to Existing Code

| Existing piece | Role in the framework |
|---|---|
| `Application` / `DeviceState` | Application source of truth for runtime mode |
| `Display::SetEmotion` / board display paths | Publish emotion and related display cues |
| Board interaction helpers (`mhaibot_interaction_model`) | Touch / sleep / alert policies that feed intent |
| `MhaiBotFaceV2` | Extraction baseline: LVGL eye objects, tick timer, pose interpolation, transients |

`EyeActivity` is **derived** from application/display inputs through an
adapter (`ADR-002` / `02` tables). The eye system must not re-litigate
legal `DeviceState` transitions. Board sleep/wake and backlight remain
outside the renderer (`ADR-003`). Blink openness is a composition
multiplier, not an `EyeFrame` field (`ADR-004`). Intent crosses tasks
via a mutex latest-wins mailbox (`ADR-005`). Shared extraction waits on
`ADR-006` exit criteria.

------------------------------------------------------------------------

## High-level Architecture

```text
Application DeviceState + display/interaction inputs
              │
              ▼  adapter (board-local)
         EyeIntent snapshot
              │
              ▼
   Eye Animation Coordinator
              │
              ├── EmotionController
              ├── BlinkController
              └── IdleController
              │
              ▼
      EyeAnimator::Update(dt)
              │
           EyeFrame
              │
              ▼
      LVGLEyeRenderer
              │
              ▼
             LCD
```

There is **no** Transition Engine, Animation Scheduler, or Render Queue.
Composition and interpolation live in `EyeAnimator`. LVGL mutation lives
only in the thin renderer.

------------------------------------------------------------------------

## Canonical Data Model (summary)

Full definitions live in `05` and `06`. Summary only:

```cpp
struct EyeIntent {
    EyeActivity activity;
    EyeEmotion emotion;
    float look_x;
    float look_y;
    bool blink_allowed;
};

struct EyeGeometry {
    float center_x;
    float center_y;
    float width;
    float height;
    float corner_radius;
    float rotation_degrees;
};

struct EyeFrame {
    EyeGeometry left;
    EyeGeometry right;
    float opacity;
};
```

------------------------------------------------------------------------

## Design Principles

- Single responsibility per layer; animator has no LVGL dependency.
- Renderer only applies `EyeFrame`; it does not decide emotion or blink.
- State-driven intent; time-based animation (`Update(delta_ms)`).
- Stereo geometry matching the existing two-eye LVGL face.
- Board-local extract first; hardware-independent geometry logic inside
  the animator/controllers.
- Easy to extend with new emotions after the core extract is stable.

------------------------------------------------------------------------

## Success Criteria

- Documentation set is internally consistent with `05`/`06`/`07`.
- Extraction preserves existing Freenove eye behavior until each behavior
  is deliberately replaced and hardware-validated.
- Smooth, watchdog-safe animation with no per-frame heap allocation.
- Clear path to unit-test geometry composition without LVGL.
