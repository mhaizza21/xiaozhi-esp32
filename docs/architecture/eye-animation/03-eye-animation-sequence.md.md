# Eye Animation Sequence & Timing

**Status:** Draft  
**Canonical sources:** `05-state-machine.md`, `06-rendering-pipeline.md`,
`07-animation-spec.md`

## Purpose

Describe the runtime update sequence and timing model for the Freenove
eye extract. Timing numbers for blinks, idle events, and emotion
transitions live in `07`; state priority lives in `05`; pipeline stages
live in `06`.

------------------------------------------------------------------------

## Runtime Flow

```text
Application DeviceState / Display::SetEmotion / board interaction
      │
      ▼  adapter
EyeIntent published to mailbox
      │
      ▼  LVGL-safe display tick
Coordinator consumes latest intent
      │
      ├── EmotionController contribution
      ├── BlinkController contribution
      └── IdleController contribution
      │
      ▼
EyeAnimator::Update(delta_ms) → EyeFrame
      │
      ▼
LVGLEyeRenderer::Render(frame)
      │
      ▼
LCD (via existing LVGL refresh)
```

There is no separate Transition Engine, Animation Scheduler, or Render
Queue in this sequence.

------------------------------------------------------------------------

## Per-Tick Timeline (time-based)

Do **not** assume a fixed 60 FPS loop. Use elapsed time from the
existing display / face timer (today roughly ~33 ms ticks in
`MhaiBotFaceV2::Config::tick_ms`, subject to load).

```text
delta_ms = now - last_tick
        ↓
ConsumeLatest(EyeIntent)
        ↓
Coordinator / controllers Update(delta_ms)
        ↓
EyeAnimator::Update(delta_ms) → EyeFrame
        ↓
LVGLEyeRenderer::Render(EyeFrame)
```

Interpolation uses:

```text
progress = elapsed / duration
```

not a fixed per-frame increment. If a tick is late, animation advances
by the real elapsed time (`06` §7 / §10).

Performance targets (`06`):

- 30 FPS minimum for acceptable motion
- 60 FPS only if the SPI LCD path already supports it without watchdog
  or jank pressure

------------------------------------------------------------------------

## Activity Sequence (derived)

Primary activity flow is defined in `05`. The eye system follows
adapter-derived `EyeActivity`; it does not own application transition
legality.

```text
Booting
  ↓
Idle
  ⇄ Listening / Thinking / Speaking
  ↓
Sleeping
  ↓
Waking
  ↓
Idle

Error may interrupt any state.
```

Naming uses `Speaking` / `Sleeping` / `Waking` (not Talking / Sleep /
Wake) to match `05`.

------------------------------------------------------------------------

## Priority During Overlap

When multiple desires compete inside one tick (`05` §7):

```text
Error
  > Sleep / Wake transition
  > Direct user interaction
  > Speaking / Listening / Thinking
  > Emotion transition
  > Idle animation
```

Example: if an idle glance is active and listening begins, cancel or
blend out the glance, preserve current geometry as the transition
start, move toward the Listening pose, and keep auto-blink unless
explicitly suppressed.

------------------------------------------------------------------------

## Blink and Sleep Nested Sequences

Blink sub-states (`05` §8): Idle → Closing → Closed → Opening → Idle.

Sleep/wake (`05` §9): Awake → ClosingForSleep → Sleeping →
OpeningFromSleep → Awake.

These are controller / animator concerns, not separate global engines.
Timing ranges are in `07`.

------------------------------------------------------------------------

## Thread Boundary

```text
Audio / network / application tasks
        │ publish EyeIntent only
        v
  Intent mailbox (latest snapshot)
        │
        v
 Display / LVGL-safe context
        │ Update + Render
        v
      LVGL objects
```

Cross-task producers never call the renderer.

------------------------------------------------------------------------

## Migration Sequence Overlay

While extracting from `MhaiBotFaceV2`, the same tick may temporarily
run legacy pose paths alongside the new `EyeFrame` adapter. Visible
behavior must remain unchanged until each roadmap step deliberately
switches a behavior (`06` §11). After hardware validation, legacy
interpolation paths that the animator replaced are removed.
