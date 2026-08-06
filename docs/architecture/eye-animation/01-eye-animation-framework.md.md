# Eye Animation Framework Architecture

**Version:** 1.1 (Draft)  
**Status:** Planning (aligned with `05` / `06` / `07`)  
**Implementation approach:** Board-local extraction from `MhaiBotFaceV2`

------------------------------------------------------------------------

# 1. Goal (ELI10)

Imagine MhaiBot is a person.

- **Application / voice session** = what the robot is doing
- **Eyes** = face expression
- **LVGL** = painter

The application should say what is happening and how it should feel. It
should **not** know how to draw eyes.

```text
DeviceState + emotion / gaze cues
        ↓ adapter
    EyeIntent
        ↓
  EyeAnimator (+ controllers)
        ↓
    EyeFrame
        ↓
  LVGLEyeRenderer
        ↓
       LCD
```

------------------------------------------------------------------------

# 2. High-Level Architecture

Matches `06` pipeline overview. Controllers contribute in **parallel**;
they are not a serial Emotion → Blink → Idle chain that each owns the
full pose.

```text
+-------------------------------+
| Application / Display paths   |
| DeviceState + SetEmotion/...  |
+---------------+---------------+
                |
                | adapter publishes EyeIntent snapshot
                v
+---------------+---------------+
| Eye Animation Coordinator     |
+---------------+---------------+
                |
     +----------+----------+
     |          |          |
     v          v          v
 EmotionCtrl BlinkCtrl IdleCtrl
     |          |          |
     +----------+----------+
                |
                v
+---------------+---------------+
| EyeAnimator::Update(delta_ms) |
+---------------+---------------+
                |
            EyeFrame
                |
                v
+---------------+---------------+
| LVGLEyeRenderer::Render()     |
+---------------+---------------+
                |
               LCD
```

Layers that are **explicitly out of scope**:

- Transition Engine
- Animation Scheduler
- Render Queue

------------------------------------------------------------------------

# 3. Layer Responsibilities

| Layer | Responsibility |
|---|---|
| Adapter | Map `DeviceState`, display emotion strings, and board interaction cues to `EyeIntent`. Does not draw. Does not own legal `DeviceState` transitions. |
| Eye Animation Coordinator | Accept latest `EyeIntent`; apply priority rules from `05`; forward stable inputs to controllers. Does not draw or create LVGL objects. |
| EmotionController | Base size/shape for the active emotion |
| BlinkController | Openness multiplier and blink timing / suppression |
| IdleController | Small gaze/position offsets when allowed |
| EyeAnimator | Compose contributions, interpolate over time, emit `EyeFrame` |
| LVGLEyeRenderer | Apply `EyeFrame` to pre-created left/right LVGL objects |

------------------------------------------------------------------------

# 4. Data Flow

```text
DeviceState / display emotion / interaction
  ↓ adapter
EyeIntent (mailbox / immutable snapshot)
  ↓ coordinator + controllers
EyeAnimator::Update(delta_ms)
  ↓
EyeFrame
  ↓
LVGLEyeRenderer
  ↓
LCD
```

Composition order for geometry (from `07` §9; **ADR-004**):

```text
Base emotion pose
  → activity adjustment
  → gaze / idle offset
  → blink openness multiplier (scales height; not stored on EyeFrame)
  → safety clamps
  → EyeFrame
```

------------------------------------------------------------------------

# 5. Directory Layout (board-local)

First implementation stays under the Freenove board. Do **not** create
`main/display/eye/` until a second board needs shared ownership.

Suggested evolution inside the existing board tree (names illustrative):

```text
main/boards/freenove-esp32s3-display-2.8-lcd/
├── mhaibot_face_v2.h / .cc          # extraction baseline (legacy face)
├── mhaibot_display.h / .cc          # Display::SetEmotion / Show-Hide
├── mhaibot_interaction_model.*      # touch / sleep / alert helpers
└── eye/                             # introduced incrementally during extract
    ├── eye_intent.h
    ├── eye_frame.h
    ├── eye_animator.h / .cc
    ├── emotion_controller.h / .cc
    ├── blink_controller.h / .cc
    ├── idle_controller.h / .cc
    └── lvgl_eye_renderer.h / .cc
```

Exact file names may follow the PR that introduces each piece. The
important constraint is **board-local ownership**.

------------------------------------------------------------------------

# 6. Core Data Structures

Canonical definitions are in `05` and `06`. Do not reintroduce a flat
intent enum or a single-eye frame.

## EyeIntent

```cpp
struct EyeIntent {
    EyeActivity activity;
    EyeEmotion emotion;
    float look_x;
    float look_y;
    bool blink_allowed;
};
```

`EyeActivity`, `EyeEmotion`, and `EyeTransient` are defined in `05`.

## EyeFrame

```cpp
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

Migration note: today’s `MhaiBotFaceV2::Pose` (left/right x, y, width,
height, radius) maps naturally onto stereo `EyeGeometry`. Openness is
composed into height / related fields before the frame is emitted;
opacity remains a frame-level field as in `06`.

------------------------------------------------------------------------

# 7. Update Loop

The animation system uses **elapsed time**, not a fixed frame-count
assumption.

```text
display / LVGL-safe tick
        ↓
consume latest EyeIntent
        ↓
EyeAnimator::Update(delta_ms)
        ↓
LVGLEyeRenderer::Render(frame)
```

Targets from `06`:

- 30 FPS minimum for normal operation
- 60 FPS only if existing display performance supports it
- correct behavior when frames are delayed

Rules:

- Controllers never draw.
- Renderer never decides emotion, blink schedule, or activity.
- Animator never calls LVGL.
- No per-frame heap allocation on the update path.

------------------------------------------------------------------------

# 8. State Model

Primary activity states, transitions, blink sub-machine, and sleep/wake
are defined in `05`. Summary:

```text
Booting → Idle ⇄ Listening / Thinking / Speaking
Idle → Sleeping → Waking → Idle
Error may interrupt any state
```

`EyeActivity` is derived from application state via the adapter. The eye
framework does not replace `DeviceState` as the application source of
truth.

------------------------------------------------------------------------

# 9. Design Rules

1. Renderer must never contain emotion or transition policy.
2. Animator must not call LVGL.
3. Controllers must not draw or touch LVGL.
4. Only Animator → Renderer communication uses `EyeFrame`.
5. Cross-task producers publish `EyeIntent` snapshots only.
6. Keep code board-local until **ADR-006** exit criteria are met (second
   consumer + stable types + no Freenove-only policy in shared code +
   ADR amendment for the shared path).
7. Extract from `MhaiBotFaceV2`; do not delete legacy face logic until
   hardware validation passes (`06` §11 step 9).

------------------------------------------------------------------------

# 10. Future Extensions

Supported after the core extract is stable (`07` §13 remains out of
scope for v1):

- Camera-based gaze
- Servo head coordination
- Shared extraction for a second board
- Additional emotions or transient reactions
- Alternate renderers only if a second display backend appears

RoboEyes may inspire timing/feel. Do not copy GPL implementation into
this framework.

------------------------------------------------------------------------

# 11. Implementation Roadmap

There is exactly one sequence. It matches `06` §11, `04`, and `08`:

1. Inspect current MhaiBot display and interaction model.
2. Identify existing LVGL eye objects and ownership.
3. Introduce `EyeFrame` without changing visible behavior.
4. Implement renderer adapter for the existing eye style.
5. Introduce `EyeAnimator` with neutral static output.
6. Add blink.
7. Add idle movement.
8. Add emotion mapping.
9. Remove replaced legacy animation logic only after hardware validation.

------------------------------------------------------------------------

# 12. AI / PR Workflow

Implement one roadmap step (or a small contiguous pair) per PR.

- Touch only assigned board-local files.
- Preserve visible behavior unless the PR explicitly changes it.
- Prefer adapter + extract over rewrite.
- Build and, when possible, run host-side unit tests for geometry
  composition with a fixed seed.
