# Eye Animation API Specification

**Status:** Draft (mapping / ownership / mailbox decided — ADR-002…006)  
**Canonical sources:** `05-state-machine.md`, `06-rendering-pipeline.md`,
`07-animation-spec.md`

## Purpose

Define stable interfaces between the eye-animation extract and the rest
of MhaiBot: concrete types, ownership, method signatures, and the
single-writer intent mailbox / threading contract.

This API is the board-local contract for the Freenove extract from
`MhaiBotFaceV2`. It is not yet a shared `main/display/` public surface.

------------------------------------------------------------------------

## Core Types

### EyeActivity / EyeEmotion / EyeTransient

Defined in `05`. Activity and emotion are separate dimensions.
Transients (blink, glance, touch reaction) may run while activity and
emotion remain unchanged.

### EyeIntent

Structured intent snapshot. Producers publish this; the display owner
consumes the latest value.

```cpp
struct EyeIntent {
    EyeActivity activity = EyeActivity::Idle;
    EyeEmotion emotion = EyeEmotion::Neutral;
    float look_x = 0.0f;   // -1 left … +1 right
    float look_y = 0.0f;   // -1 up … +1 down
    bool blink_allowed = true;
};
```

Invalid values are normalized by the coordinator / animator (unknown
activity → Idle, unknown emotion → Neutral, non-finite gaze → 0, etc.).

### EyeGeometry / EyeFrame

Defined in `06`. Stereo geometry for the two persistent eye objects.

```cpp
struct EyeGeometry {
    float center_x = 0.0f;
    float center_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float corner_radius = 0.0f;
    float rotation_degrees = 0.0f;
};

struct EyeFrame {
    EyeGeometry left{};
    EyeGeometry right{};
    float opacity = 1.0f;
};
```

------------------------------------------------------------------------

## DeviceState → EyeActivity Adapter

`DeviceState` remains the application source of truth. The adapter maps
application/display inputs into `EyeIntent` fields. Mapping is
board-owned and must live in **one** place next to the Freenove display
(`MhaiBotDisplay` / `EyeActivityAdapter`). Do not invent a second legal
`DeviceState` transition table. Canonical decision: **ADR-002**.

### Base map: `DeviceState` → `EyeActivity`

| `DeviceState` | `EyeActivity` |
|---|---|
| `kDeviceStateUnknown` | `Idle` (normalize) |
| `kDeviceStateStarting` | `Booting` |
| `kDeviceStateWifiConfiguring` | `Booting` |
| `kDeviceStateActivating` | `Booting` |
| `kDeviceStateUpgrading` | `Booting` |
| `kDeviceStateIdle` | `Idle` |
| `kDeviceStateConnecting` | `Idle` |
| `kDeviceStateListening` | `Listening` |
| `kDeviceStateSpeaking` | `Speaking` |
| `kDeviceStateAudioTesting` | `Idle` |
| `kDeviceStateFatalError` | `Error` |

### Ordered activity overrides (apply after base)

1. Board sleeping face (`sleeping_face_active_` /
   `SetEmotion("sleeping"|"sleep")`) → `Sleeping`.
2. Board groggy wake (`StartGroggyWake` / groggy active) → `Waking`.
3. Emotion `"thinking"` / `"confused"` while base is `Idle` or
   `Listening` → `Thinking` (no dedicated `DeviceState` today).
4. Emotion `"listening"` → `Listening` (legacy cue; app Listening entry
   actually sends status + `"neutral"`, remapped today via
   `listening_status_active_`).
5. Emotion `"speaking"` does **not** override activity; use
   `kDeviceStateSpeaking`.

`blink_allowed`: default `false` for `Sleeping`, `Waking`, `Error`,
`Booting`; `true` otherwise.

### Emotion strings → `EyeEmotion`

| `SetEmotion` string(s) | `EyeEmotion` |
|---|---|
| `neutral`, `robot_2`, unrecognized | `Neutral` |
| `happy`, `laughing`, `notification`, `excited` | `Happy` |
| `thinking`, `confused`, `warning`, `listening`, `confident` | `Focused` |
| `speaking` (LLM expression while Speaking) | `Neutral` |
| `relaxed`, `sleepy`, `sleeping`, `sleep` | `Sleepy` |
| `sad` | `Sad` |
| `angry` | `Angry` |
| `surprised` | `Surprised` |

Error/alert strings (`error`, `cancel`, `cloud_off`) keep eye emotion
`Neutral` and use display alert chrome; `warning` → `Focused` as above.
Touch reactions map to `EyeTransient` / coordinator inputs without
bypassing LVGL thread rules.

------------------------------------------------------------------------

## Intent Mailbox / Threading Contract

### Rules

1. **Single writer of the published intent slot** from the display/UI
   owner’s perspective: producers may call a thread-safe
   `PublishIntent`, but only the LVGL-safe display context **consumes**
   and applies intent into the animator.
2. `EyeIntent` is treated as an **immutable snapshot**. Writers publish
   a complete struct; readers copy the latest value once per update.
3. **No shared mutable animation internals** across tasks (`05` §11).
4. `EyeAnimator::Update` and `LVGLEyeRenderer::Render` run only in the
   existing display / LVGL-safe update context (today: face timer /
   display path that already mutates LVGL).
5. Audio, network, protocol, servo, and ISR contexts must **not** call
   LVGL or the renderer. They may only publish intent (or schedule work
   onto the display owner via existing `Application::Schedule` /
   display APIs).

### Concrete mailbox API (ADR-005)

v1 uses a **mutex-protected latest-wins slot** (same synchronization
style as multi-field protection in `DeviceStateMachine`; Freenove keeps
POD flags as `std::atomic` and hops to the main task with
`Application::Schedule` before LVGL work).

```cpp
class EyeIntentMailbox {
public:
    // Any task: publish a complete snapshot. Overwrites previous.
    // Must not allocate. Must not call LVGL or DisplayLockGuard.
    void Publish(const EyeIntent& intent);

    // Display/UI owner only: copy the latest snapshot.
    // Returns true if a value was available (including unchanged).
    bool ConsumeLatest(EyeIntent* out) const;

private:
    mutable std::mutex mutex_;
    EyeIntent latest_{};
    bool has_value_ = false;
};
```

Semantics:

- Latest-wins; no queue of historical intents.
- Torn reads are unacceptable; publish/consume the whole struct under
  the mutex.
- Dropping intermediate intents under load is acceptable if the latest
  published value is always visible.
- `DisplayLockGuard` / `lvgl_port_lock` protect LVGL mutations only —
  they are **not** the intent mailbox.
- Prefer `Application::Schedule` to move board/touch work onto the
  display owner; Publish stores the intent snapshot for the next tick.

------------------------------------------------------------------------

## Main Components

### EyeActivityAdapter (board-local)

```cpp
class EyeActivityAdapter {
public:
    // Build intent from application/display inputs. Pure mapping.
    EyeIntent FromDeviceState(DeviceState state,
                              EyeEmotion emotion,
                              float look_x,
                              float look_y,
                              bool blink_allowed) const;
};
```

Responsibilities:

- Map inputs → `EyeIntent`
- Never draw
- Never own `DeviceState` transition legality

### EyeAnimationCoordinator

```cpp
class EyeAnimationCoordinator {
public:
    void SetIntent(const EyeIntent& intent);
    void SetTransient(EyeTransient transient);  // optional explicit trigger
    void Update(uint32_t delta_ms);
    const EyeFrame& frame() const;

private:
    EyeIntent intent_{};
    EmotionController emotion_;
    BlinkController blink_;
    IdleController idle_;
    EyeAnimator animator_;
};
```

Responsibilities:

- Accept latest intent
- Apply priority rules from `05` (Error > Sleep/Wake > interaction >
  Speaking/Listening/Thinking > emotion transition > idle)
- Forward stable inputs to controllers and animator
- Does not create or mutate LVGL objects

### EmotionController

```cpp
class EmotionController {
public:
    void SetEmotion(EyeEmotion emotion);
    void Update(uint32_t delta_ms);
    // Contribution used by EyeAnimator (base geometry / shape params).
};
```

Never draws. Never accesses LVGL.

### BlinkController

```cpp
class BlinkController {
public:
    void SetAllowed(bool allowed);
    void Trigger(EyeTransient kind);  // Blink or DoubleBlink
    void Update(uint32_t delta_ms);
    float openness_multiplier() const;  // typically 0…1
};
```

Owns blink timing and suppression rules from `05` §8 and `07` §5.

**Openness model (ADR-004):** `openness_multiplier` is applied by
`EyeAnimator` during composition (`07` §9) by scaling effective eye
height **before** emit. `EyeFrame` does **not** store an openness
field — only final stereo geometry + opacity. Emotion/activity base
poses may still use reduced height for Sleepy/Sleeping; that is base
geometry, not blink.

### IdleController

```cpp
class IdleController {
public:
    void SetEnabled(bool enabled);
    void Update(uint32_t delta_ms);
    float look_offset_x() const;
    float look_offset_y() const;
};
```

Event-driven micro gaze / glance (`07` §6). No per-frame random
selection.

### EyeAnimator

```cpp
class EyeAnimator {
public:
    void Reset(const EyeFrame& frame);
    void SetTargetFromControllers(/* controller contributions + intent */);
    void Update(uint32_t delta_ms);
    const EyeFrame& frame() const;
};
```

Rules:

- Owns interpolation / current geometric state
- No LVGL dependency
- Emits clamped, finite `EyeFrame` values (`06` §5, `07` §11)

### LVGLEyeRenderer

```cpp
class LVGLEyeRenderer {
public:
    // Bind to pre-created objects from MhaiBotFaceV2 extract.
    void Init(lv_obj_t* left_eye, lv_obj_t* right_eye);
    void Render(const EyeFrame& frame);
    void Deinit();
};
```

May update position, size, radius, rotation (if used), opacity, and
invalidate changed objects. Must not decide emotions, schedule blinks,
own assistant state, or allocate/recreate eye objects every frame.

### Ownership map (ADR-003)

| Concern | v1 owner |
|---|---|
| `left_eye_` / `right_eye_` property updates from `EyeFrame` | `LVGLEyeRenderer` |
| LVGL object lifetime (`root_`, eyes, `sleep_label_`) | Face shell / `MhaiBotFaceV2` (or `MhaiBotEyeSystem`) |
| `sleep_label_` text / show-hide | Face shell, when `EyeActivity::Sleeping` |
| Face `Show` / `Hide` (root flag + tick pause/resume) | Face shell / `MhaiBotDisplay` |
| Backlight brightness / restore | Board (`FreenoveESP32S3Display` / `MhaiBotBacklight`) |
| Panel on/off | Board + `MhaiBotDisplay::SetPanelPowered` |
| Alert label / reaction emoji | `MhaiBotDisplay` |

The renderer’s v1 job is the two eye objects only.

------------------------------------------------------------------------

## Display Owner Facade (suggested)

A thin façade can wrap mailbox + coordinator + renderer for the board
display class:

```cpp
class MhaiBotEyeSystem {
public:
    void Init(lv_obj_t* parent, lv_color_t eye_color);
    void Destroy();

    // Thread-safe publish (or display-task-only during early extract).
    void PublishIntent(const EyeIntent& intent);

    // LVGL-safe tick: consume mailbox, Update, Render.
    void Tick(uint32_t delta_ms);

    void Show();
    void Hide();
};
```

During migration, `MhaiBotFaceV2` may implement this façade
incrementally while legacy pose paths still exist.

------------------------------------------------------------------------

## Communication Summary

| From | To | Payload |
|---|---|---|
| Application / display / interaction | Mailbox | `EyeIntent` snapshot |
| Mailbox | Coordinator (UI task) | `EyeIntent` |
| Controllers | Animator | Limited contributions (not full frames) |
| Animator | Renderer | `EyeFrame` |
| Renderer | LVGL | Object property updates |

Components do **not** all communicate via `EyeFrame`. Only the
animator→renderer edge uses `EyeFrame`.

------------------------------------------------------------------------

## Shared vs board-local (ADR-006)

This API remains board-local under
`main/boards/freenove-esp32s3-display-2.8-lcd/` until **all** of:

1. A second board/display consumer needs the same animator/controllers.
2. Extracted types/APIs are stable after Freenove hardware-validated
   extract work.
3. Shared code would contain no Freenove-only policy (sleep_label,
   backlight curves, panel power, pet/startle/groggy entry points).
4. An ADR amendment names the shared path (candidate: `main/display/eye/`).

## Non-Goals for this API surface

- Shared `main/display/eye/` headers before ADR-006 exit criteria
- Network-scripted custom animations
- Render queue / dirty-region scheduler APIs
- Flat `EyeIntent` enum or single-eye frame types
- Storing blink openness on `EyeFrame`
