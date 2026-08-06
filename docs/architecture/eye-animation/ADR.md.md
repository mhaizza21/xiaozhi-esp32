# ADR-001: Eye Animation Framework

**Status:** Accepted (documentation alignment)  
**Scope:** MhaiBot Eye Animation Framework  
**Canonical companions:** `05-state-machine.md`, `06-rendering-pipeline.md`, `07-animation-spec.md`

## Context

The Freenove board already ships a working eyes-only face in
`MhaiBotFaceV2` (persistent LVGL eye objects, timer tick, pose
interpolation, emotion targets, and transient animations such as
petting / groggy wake / startled). Application emotion and status
continue to flow through `Display::SetEmotion` and related board display
paths. Application runtime state remains owned by
`Application::SetDeviceState()` / the device state machine.

Earlier draft docs described a flat `EyeIntent` enum, a single-eye
`EyeFrame`, and phantom layers (Transition Engine, Animation Scheduler,
Render Queue). Those conflict with the stronger design in `05`/`06`/`07`
and with the existing board-local face.

Without one agreed model, incremental PRs would thrash types and
ownership.

## Decision

1. **Canonical types** are those defined in `05` and `06`:
   - Structured `EyeIntent` (`EyeActivity`, `EyeEmotion`, `look_x`,
     `look_y`, `blink_allowed`).
   - Stereo `EyeFrame` (`EyeGeometry left`, `EyeGeometry right`,
     `opacity`).
   - Orthogonal dimensions: activity, emotion, and `EyeTransient`
     (blink / glance / touch reaction), not a single flat enum.

2. **Layering** follows `06`:
   - Interaction / adapter publishes an immutable `EyeIntent` snapshot.
   - A thin animation coordinator applies priority and feeds controllers.
   - Controllers (`EmotionController`, `BlinkController`,
     `IdleController`) contribute limited pose parts in parallel.
   - `EyeAnimator::Update(delta_ms)` composes and interpolates to an
     `EyeFrame`.
   - `LVGLEyeRenderer` applies that frame to pre-created LVGL objects.

3. **Simplification:** one animator plus a thin renderer. Do **not**
   introduce a Transition Engine, Animation Scheduler, or Render Queue.

4. **Migration strategy:** extract and refactor from `MhaiBotFaceV2`
   (Pose → `EyeFrame`, emotion/activity mapping → `EyeIntent`), preserving
   existing LVGL objects and update context. This is not a greenfield
   rewrite under `main/display/eye/`.

5. **Ownership:** keep the first implementation board-local under
   `main/boards/freenove-esp32s3-display-2.8-lcd/`. Shared display modules
   wait on **ADR-006** exit criteria.

6. **Application state:** `DeviceState` remains the application source of
   truth. Map it (and display emotion strings) to `EyeActivity` /
   `EyeEmotion` through an adapter (**ADR-002**). Do not duplicate legal
   transition policy inside the eye system.

7. **Threading:** mutex-protected latest-wins intent mailbox; only the
   LVGL-safe display owner runs `Update` / `Render` and mutates LVGL
   objects (**ADR-005**; see also `05` §11 and `06` §8).

8. **Implementation order:** follow `06` §11 exclusively
   (Frame → renderer adapter → animator → blink → idle → emotion →
   remove legacy after hardware validation). `04` and `08` describe that
   same sequence.

```text
DeviceState / Display emotion / board interaction
              │
              ▼  adapter
         EyeIntent (snapshot / mailbox)
              │
              ▼
   Animation coordinator + controllers
              │
              ▼
      EyeAnimator::Update(dt)
              │
              ▼
           EyeFrame
              │
              ▼
      LVGLEyeRenderer::Render()
              │
              ▼
              LCD
```

## Consequences

### Benefits

- One type model across all documents and PRs.
- Clear test boundary: animator/controllers without LVGL.
- Replaceable renderer over stable geometry.
- Incremental extraction from shipping face code reduces regression risk.
- Matches AGENTS.md board-local ownership rules for the first consumer.

### Trade-offs

- More explicit types and adapter mapping than today’s face `Emotion` enum.
- Board-local duplication risk until a second consumer justifies shared
  extraction.
- Requires discipline: no new animation logic in LVGL-only code, and no
  second parallel state machine for application transitions.

## Rejected Alternatives

| Alternative | Why rejected |
|---|---|
| Flat `EyeIntent` enum mixing activity and emotion | Cannot represent Listening + Focused + Blink simultaneously; contradicts `05`/`06` |
| Single-eye `EyeFrame` with pupil fields | Does not match stereo rounded-rect face or `06` geometry |
| Transition Engine / Scheduler / Render Queue | Unused by `05`/`06`/`07`; overkill for two persistent LVGL objects |
| Greenfield `main/display/eye/` before second board | Violates narrow ownership; ignores working `MhaiBotFaceV2` |
| Parallel EyeActivity state machine as application SoT | Duplicates `DeviceState`; adapter is sufficient |

## Future ADRs

Potential later decisions (not required for v1):

- Camera gaze / servo coordination
- Explicit renderer backend abstraction beyond LVGL
- Seeded RNG interface for deterministic idle tests

Shared-module extraction is gated by **ADR-006** exit criteria (not an
open future topic).

---

# ADR-002: DeviceState / SetEmotion → EyeIntent Mapping

**Status:** Accepted  
**Date:** 2026-08-06  
**Decides:** Exact `DeviceState` and `SetEmotion(...)` mapping into
`EyeIntent` (`EyeActivity` + `EyeEmotion`).

## Evidence

- `main/device_state.h` — `DeviceState` enum values.
- `main/application.cc` `HandleStateChangedEvent()` — Idle/Connecting
  set `"neutral"`; Listening sets `SetStatus(LISTENING)` then
  `"neutral"`; Speaking sets status only (emotion arrives via LLM
  `SetEmotion`).
- `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_display.cc`
  `ToFaceEmotion` + `listening_status_active_` remap of Listening +
  `"neutral"` → face Listening pose.
- Freenove board `freenove-esp32s3-display-2.8-lcd.cc` — sleep face via
  `SetEmotion("sleeping")` / `sleeping_face_active_`; groggy via
  `StartGroggyWake()` (not a `DeviceState`).
- Canonical activity/emotion enums in `05`.

## Decision

`DeviceState` is the **primary** source for `EyeActivity`. Board sleep /
wake and legacy activity-encoded emotion strings are **ordered
overrides**. `Display::SetEmotion` strings primarily map to
`EyeEmotion`. Full tables live in `02-eye-animation-api.md`.

### DeviceState → EyeActivity (base)

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

### Ordered activity overrides (after base)

1. Board screen-off / fatal display error cues → `Error` if already
   indicated by alert/error emotion path; else keep base.
2. Board `sleeping_face_active_` / emotion `"sleeping"` / `"sleep"` →
   `Sleeping`.
3. Board groggy wake active / `StartGroggyWake` → `Waking`.
4. Emotion `"thinking"` / `"confused"` while base activity is `Idle` or
   `Listening` → `Thinking` (no `DeviceState` for thinking today).
5. Emotion `"listening"` → `Listening` (legacy cue).
6. Emotion `"speaking"` does **not** override activity; Speaking comes
   from `kDeviceStateSpeaking`.

`blink_allowed` defaults: `false` for `Sleeping`, `Waking`, `Error`,
`Booting`; `true` otherwise (coordinator may still suppress per `05` §8).

### Emotion strings → EyeEmotion

| Emotion string(s) | `EyeEmotion` |
|---|---|
| `neutral`, `robot_2`, `speaking` (expression), unrecognized | `Neutral` |
| `happy`, `laughing`, `notification`, `excited` | `Happy` |
| `thinking`, `confused`, `warning`, `listening`, `confident` | `Focused` |
| `relaxed`, `sleepy`, `sleeping`, `sleep` | `Sleepy` |
| `sad` | `Sad` |
| `angry` | `Angry` |
| `surprised` (and startled transient expression) | `Surprised` |

Error/alert strings (`error`, `cancel`, `cloud_off`) keep emotion
`Neutral` (or `Focused` for `warning`) and rely on display alert chrome /
`EyeActivity::Error` when the fatal path applies — matching today's
`SetAlertState` split from eye geometry.

Adapter ownership: board-local next to `MhaiBotDisplay` (e.g.
`EyeActivityAdapter` / `ToFaceEmotion` successor). One place only.

## Rejected alternatives

| Alternative | Why rejected |
|---|---|
| Eye system re-implements `DeviceState` transitions | Duplicates SoT; violates ADR-001 |
| Flat map of emotion string → activity only | Breaks Listening (`neutral` + status) and Speaking (status without emotion) |
| `kDeviceStateConnecting` → `Listening` | Code shows neutral / Idle face today |
| Encode Listening only via emotion `"listening"` | App never sends that string on state entry |

## Risks

- LLM emotion storms during Speaking can change `EyeEmotion` rapidly;
  coordinator must keep activity `Speaking`.
- `Thinking` activity depends on emotion strings until a first-class
  app cue exists — residual uncertainty, non-blocking.
- Board sleep can override Idle while `DeviceState` remains Idle —
  intentional; document in adapter tests.

## Docs to update

- `02-eye-animation-api.md` (canonical tables)
- `00-overview.md`, `01-eye-animation-framework.md` (pointer only)
- This ADR

---

# ADR-003: sleep_label / Backlight / Show-Hide Ownership

**Status:** Accepted  
**Date:** 2026-08-06  
**Decides:** Which layer owns `sleep_label`, backlight/panel power, and
face `Show`/`Hide`.

## Evidence

- `mhaibot_face_v2.cc` — creates/updates `sleep_label_` during Sleeping
  tick via `MhaiBotSleepText`; `Show`/`Hide` toggle `root_` + pause/resume
  `lv_timer`.
- `mhaibot_display.cc` — `ApplyFaceVisibility` calls face Show/Hide;
  reaction emoji / alert labels are display-owned; all LVGL mutations
  under `DisplayLockGuard`.
- `freenove-esp32s3-display-2.8-lcd.cc` — backlight brightness, panel
  on/off, sleep generation, groggy brightness curve owned by the board
  + `MhaiBotBacklight`, scheduled via `Application::Schedule`.
- `02` / `06` — renderer v1 is the two eye objects only.

## Decision

| Concern | Owner (v1) | Not owned by |
|---|---|---|
| `left_eye_` / `right_eye_` property updates | `LVGLEyeRenderer` | Animator / adapter |
| Object lifetime (`root_`, eyes, `sleep_label_`) | Face shell / `MhaiBotFaceV2` (or `MhaiBotEyeSystem` façade) | Renderer (binds refs only) |
| `sleep_label_` text / visibility | Face shell, driven by `EyeActivity::Sleeping` (+ existing sleep-text helper) | `LVGLEyeRenderer`, backlight |
| Face `Show` / `Hide` (root visibility, tick pause) | Face shell / display façade (`MhaiBotDisplay`) | Animator, renderer, board power policy |
| Backlight brightness / restore | Board (`FreenoveESP32S3Display` / `MhaiBotBacklight`) | Eye animator/renderer |
| Panel powered on/off | Board + `MhaiBotDisplay::SetPanelPowered` | Eye animator/renderer |
| Alert / reaction emoji overlays | `MhaiBotDisplay` | Eye renderer |

Eye framework publishes/consumes intent and renders eye geometry only.
Sleep Zzz chrome and power policy stay outside `LVGLEyeRenderer`.

## Rejected alternatives

| Alternative | Why rejected |
|---|---|
| Renderer owns `sleep_label_` | Couples text UX into geometry apply path; contradicts `02`/`06` |
| Animator owns backlight | Power policy is board/hardware; animator must stay LVGL-free and testable |
| Move Show/Hide into animator | Visibility is display lifecycle, not pose composition |

## Risks

- Sleep face + dim backlight can desync if board and shell diverge —
  keep board as single caller of both `SetEmotion("sleeping")` and
  brightness.
- Preview/hide paths in `MhaiBotDisplay::SetPreviewImage` must continue
  to call shell Show/Hide, not the renderer.

## Docs to update

- `02-eye-animation-api.md`
- `08-implementation-plan.md` Phase 2 checklist
- This ADR

---

# ADR-004: Blink Openness Composition Model

**Status:** Accepted  
**Date:** 2026-08-06  
**Decides:** Blink openness is a **composition multiplier**, not a field
stored on `EyeFrame`.

## Evidence

- `07` §9 composition order: base emotion → activity → gaze/idle →
  **blink openness** → clamps → `EyeFrame`.
- `06` §4.3: `BlinkController → openness multiplier`.
- `EyeFrame` in `06`/`02` has stereo geometry + `opacity` only — no
  openness member.
- `MhaiBotFaceV2` has no separate blink controller yet; sleep/groggy
  fold closure into `Pose.height` (and groggy applies a height clamp
  after pose interpolate). V1 `MhaiBotFace` blink collapsed open-span
  geometry similarly (effect, not a frame field).

## Decision

- `BlinkController` exposes `openness_multiplier()` in `[0, 1]`.
- `EyeAnimator` applies that multiplier **during composition**, scaling
  effective eye height (and any blink-owned vertical adjust) **before**
  emitting `EyeFrame`.
- `EyeFrame` remains final drawable geometry + opacity only.
- Emotion/activity base poses may still use reduced height for Sleepy /
  Sleeping expressions; that is base geometry, not blink openness.
- Sleeping / Waking / openness-owning transitions suppress auto-blink
  (`05` §8, `07` §5.3).

## Rejected alternatives

| Alternative | Why rejected |
|---|---|
| Store `openness` on `EyeFrame` | Conflicts with `06` frame model; pushes policy into renderer |
| Bake blink only into emotion height with no multiplier | Cannot run blink orthogonally with emotion/activity (`05`) |
| Renderer computes blink | Violates renderer purity |

## Risks

- Migrating Freenove sleep/groggy height hacks to activity base +
  multiplier needs hardware parity checks.
- Double-application (base sleepy height × blink) can over-close —
  suppression rules must hold during Sleeping/Waking.

## Docs to update

- `02-eye-animation-api.md` (BlinkController contract clarification)
- `01-eye-animation-framework.md` (composition note already matches;
  add explicit “not on EyeFrame”)
- This ADR
- Do **not** change `05`/`06`/`07` models

---

# ADR-005: EyeIntent Mailbox Primitive

**Status:** Accepted  
**Date:** 2026-08-06  
**Decides:** Concrete cross-task intent publication primitive.

## Evidence

- Freenove board: touch task uses `std::atomic` POD flags +
  `Application::Schedule` before display/face calls.
- Display LVGL mutations: `DisplayLockGuard` → `lvgl_port_lock`
  (`lcd_display.cc`).
- Face tick: `lv_timer` callback (LVGL task context).
- `DeviceStateMachine`: `std::atomic<DeviceState>` for the enum plus
  `std::mutex` for multi-field listener bookkeeping.
- `02` draft: latest-wins immutable snapshot; no alloc; no LVGL.

## Decision

v1 mailbox is a **mutex-protected latest-wins slot**:

```cpp
class EyeIntentMailbox {
public:
    void Publish(const EyeIntent& intent);
    bool ConsumeLatest(EyeIntent* out) const;

private:
    mutable std::mutex mutex_;
    EyeIntent latest_{};
    bool has_value_ = false;
};
```

Rules:

- Latest-wins; no history queue; torn reads forbidden.
- `Publish` may run from any task; must not call LVGL or take
  `DisplayLockGuard`.
- `ConsumeLatest` runs only in the LVGL-safe display owner tick
  (today: face `lv_timer` / display path), then coordinator
  `Update` + `Render` under the existing display lock as needed for
  LVGL.
- Prefer existing `Application::Schedule` to hop onto the main/display
  path for complex board actions; the mailbox is for the intent
  snapshot itself, not a replacement for Schedule.
- No heap allocation in Publish/Consume.

## Rejected alternatives

| Alternative | Why rejected |
|---|---|
| `DisplayLockGuard` / `lvgl_port_lock` as the mailbox | Couples producers to LVGL; deadlock risk from touch/ISR-adjacent paths |
| `Application::Schedule` alone without a slot | No durable latest snapshot if multiple publishes coalesce before tick |
| Atomic dual-buffer / seqlock | Valid, but unused in this tree for multi-field structs; mutex matches `DeviceStateMachine` synchronization style |
| FreeRTOS queue of intents | History/queueing unused; latest-wins is enough; more RAM/complexity |

## Risks

- Mutex contention is negligible at emotion/state rates; do not use from
  hard ISR (Freenove already Schedules out of touch task).
- If a future producer needs lock-free publish, revisit dual-slot under
  a new ADR without changing the `Publish`/`ConsumeLatest` API.

## Docs to update

- `02-eye-animation-api.md` (replace “suggested” with this primitive)
- This ADR

---

# ADR-006: Board-Local vs Shared Module Exit Criteria

**Status:** Accepted  
**Date:** 2026-08-06  
**Decides:** Explicit criteria before moving eye framework code into a
shared module.

## Evidence

- ADR-001 ownership: first implementation under
  `main/boards/freenove-esp32s3-display-2.8-lcd/`.
- AGENTS.md: narrowest owning layer; board-specific behavior stays out
  of core.
- Only Freenove consumes `MhaiBotFaceV2` today.

## Decision

Remain **board-local** until **all** of the following are true:

1. **Second consumer:** another board or display class needs the same
   animator/controllers/types (not only emotion emoji chrome).
2. **Stable extracted types:** `EyeIntent`, `EyeFrame`, `EyeGeometry`,
   `EyeActivity`, `EyeEmotion`, and the coordinator/controller APIs have
   shipped through at least one Freenove hardware-validated extract
   series without breaking renames.
3. **No Freenove-only policy in shared code:** sleep_label text, PWM
   backlight curves, panel on/off, pet/startle/groggy board entry
   points, and eyes-only chrome stay board- or display-shell-local.
4. **Explicit ADR amendment** approving the target shared path (expected
   candidate: `main/display/eye/`) and listing moved files.

Until then: implement under the Freenove board tree (optional `eye/`
subdir as in `01`).

## Rejected alternatives

| Alternative | Why rejected |
|---|---|
| Move to `main/display/eye/` at first extract PR | Violates ADR-001 / AGENTS.md; only one consumer |
| Share only headers immediately | Still creates core coupling before APIs stabilize |
| Never share | Unnecessarily blocks a second MhaiBot display board |

## Risks

- Premature abstraction if a second board wants different geometry —
  require stereo `EyeFrame` compatibility before moving.
- Copy-paste drift while board-local — accept until criterion (1) trips.

## Docs to update

- `02-eye-animation-api.md`, `01`, `README`, `00`, `08`
- This ADR
