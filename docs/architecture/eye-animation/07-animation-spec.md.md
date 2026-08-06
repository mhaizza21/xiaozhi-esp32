# Eye Animation Behavior Specification

**Status:** Draft  
**Scope:** Initial MhaiBot eye-animation behavior  
**Implementation status:** Planning only

---

## 1. Purpose

This document defines the expected visible behavior of the first version of the Eye Animation Framework.

It specifies behavior, timing ranges, priorities, and validation expectations. It does not prescribe the exact C++ implementation.

Values in this document are initial tuning targets and must be validated on the physical display.

---

## 2. Design Goals

The eyes should feel:

- alive but not distracting,
- smooth but responsive,
- expressive without excessive movement,
- consistent across assistant states,
- safe for the existing LVGL and ESP32 runtime.

The implementation must be original and designed for MhaiBot. RoboEyes may be used as behavioral inspiration, but its GPL implementation should not be copied into this framework.

---

## 3. Coordinate Conventions

Recommended normalized inputs:

```text
look_x: -1.0 = left,  0.0 = center, +1.0 = right
look_y: -1.0 = up,    0.0 = center, +1.0 = down
openness: 0.0 = closed, 1.0 = fully open
opacity: 0.0 = hidden, 1.0 = fully visible
```

All normalized values must be clamped before rendering.

---

## 4. Base Poses

### 4.1 Neutral

- Symmetric eyes
- Centered gaze
- Full or near-full openness
- No tilt
- Default rounded corners

### 4.2 Happy

- Slightly reduced height
- Soft upward expression
- Symmetric shape
- Relaxed blink timing

### 4.3 Sad

- Reduced openness
- Slight downward visual weight
- Slower transition than Surprised
- Avoid exaggerated continuous movement

### 4.4 Angry

- Focused, narrower shape
- Optional inward tilt
- Reduced idle-glance frequency
- Blink remains possible unless a critical pose requires otherwise

### 4.5 Surprised

- Increased height/openness
- Fast transition in
- Short hold
- Controlled transition back to the active state

### 4.6 Focused

- Slightly narrowed eyes
- Stable gaze
- Minimal idle drift
- Suitable for Listening or Thinking

### 4.7 Sleepy

- Openness approximately 0.35–0.55
- Slow movement
- Longer blink closing/closed phases
- No frequent glances

Exact geometry values must be determined after inspecting the current PR #3 minimal-eye implementation.

---

## 5. Blink Specification

### 5.1 Normal Blink

```text
Open
  ↓ 70–100 ms
Closed
  ↓ 40–80 ms
Open
  ↓ 90–130 ms
```

Auto-blink interval target:

```text
2.5–6.0 seconds
```

The interval should be randomized per event, not recalculated every frame.

### 5.2 Double Blink

Sequence:

```text
Blink
  ↓ short gap 80–160 ms
Blink
```

Double blink should be uncommon and disabled during high-priority transitions.

### 5.3 Blink Suppression

Suppress normal auto-blink during:

- Sleeping
- Waking until the wake-open transition completes
- Full-closure error animations
- Any transition that already owns openness

---

## 6. Idle Behavior

Idle behavior is event-driven, not random movement every frame.

### 6.1 Micro Gaze

Suggested range:

```text
look_x: -0.15 to +0.15
look_y: -0.08 to +0.08
```

Duration:

```text
300–900 ms
```

### 6.2 Idle Glance

- Move toward a small off-center target
- Hold briefly
- Return to center
- Do not immediately repeat

Suggested event interval:

```text
2–7 seconds
```

### 6.3 Idle Scale Pulse

Optional and very subtle:

```text
size variation: ±2–3%
```

Disable if it produces visible LCD shimmer or unnecessary invalidation.

### 6.4 Event Selection

Initial weighting example:

| Event | Suggested weight |
|---|---:|
| Normal blink | 60% |
| Micro gaze | 25% |
| Idle glance | 10% |
| Double blink | 5% |

These are tuning defaults, not hard requirements.

---

## 7. Activity Behavior

### 7.1 Idle

- Full idle controller enabled
- Normal blink enabled
- Current emotion remains visible

### 7.2 Listening

- Gaze near center
- Focused or attentive pose
- Reduced random idle movement
- Blink remains enabled

### 7.3 Thinking

- Focused pose
- Optional slow deliberate gaze offset
- No rapid random glances
- Blink interval may increase slightly

### 7.4 Speaking

- Preserve readable expression
- Avoid excessive movement that competes with speaking indicators
- Blink allowed only when it does not conflict with existing display behavior

### 7.5 Sleeping

- Eyes fully closed
- Idle and blink controllers disabled
- Renderer may reduce opacity only if coordinated with display power behavior

### 7.6 Waking

- Smooth closed-to-open transition
- Idle and auto-blink disabled until completion
- End in the requested awake activity/emotion

### 7.7 Error

- Highest-priority pose
- Stable and immediately recognizable
- Avoid uncontrolled animation
- Must not permanently corrupt the previous animation state

---

## 8. Transition Timing

Initial targets:

| Transition | Suggested duration |
|---|---:|
| Neutral ↔ Happy | 200–350 ms |
| Neutral ↔ Focused | 150–300 ms |
| Any → Surprised | 80–180 ms |
| Surprised → previous pose | 200–350 ms |
| Awake → Sleep | 400–800 ms |
| Sleep → Awake | 500–1000 ms |
| Gaze movement | 150–400 ms |

Use easing curves rather than linear motion where appropriate.

Recommended initial easing:

- position/size: ease-in-out
- surprised entry: ease-out
- sleep closing: ease-in
- wake opening: ease-out

---

## 9. Composition Rules

Suggested composition order:

```text
Base emotion pose
      ↓
Activity adjustment
      ↓
Gaze/idle offset
      ↓
Blink openness
      ↓
Safety clamps
      ↓
EyeFrame
```

Higher-priority layers may suppress lower-priority layers.

Example:

```text
Sleeping suppresses:
- idle glance
- micro gaze
- normal blink
```

---

## 10. Randomness Rules

Random behavior should be:

- seeded once,
- event-based,
- bounded,
- reproducible in tests when a fixed seed is provided.

Do not call random selection every frame.

A testable random-source interface may be introduced later if needed.

---

## 11. Safety Limits

Before rendering:

- width and height must remain positive,
- eyes must stay inside the display-safe region,
- openness must remain between 0 and 1,
- opacity must remain between 0 and 1,
- no NaN or infinite value may reach LVGL,
- left/right eyes must not overlap unless explicitly designed.

---

## 12. Hardware Validation Checklist

Test each behavior on the real Freenove ESP32-S3 display:

- Neutral
- Happy
- Sad
- Angry
- Surprised
- Focused
- Sleepy
- Normal blink
- Double blink
- Idle micro gaze
- Idle glance
- Listening
- Thinking
- Speaking
- Sleep
- Wake
- Error interruption

Verify:

- no visible one-frame jumps,
- no clipping,
- no eye overlap,
- no display flicker,
- no watchdog reset,
- no unexpected heap growth,
- existing UI/status behavior remains functional,
- acceptable frame rate and responsiveness.

---

## 13. Out of Scope for Version 1

- Camera-based eye tracking
- Servo-head coordination
- Pupil rendering unless already supported by the current design
- Full RoboEyes compatibility
- Physics simulation
- Complex animation scripting
- Network-controlled custom animations

These may be added after the core framework is stable.
