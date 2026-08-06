# Eye Animation State Machine

**Status:** Draft  
**Scope:** MhaiBot Eye Animation Framework  
**Implementation status:** Planning only

---

## 1. Purpose

This document defines how the eye system changes between high-level runtime states without mixing those states with rendering details.

The state machine answers:

- What is MhaiBot doing now?
- Which transitions are allowed?
- Which state has priority?
- Which animations may run inside each state?
- What happens when a higher-priority event interrupts the current state?

It does **not** define LVGL drawing code. Rendering is covered in `06-rendering-pipeline.md`.

---

## 2. ELI10

Think of the eye system as an actor following stage directions.

```text
The director says: "Listen"
        ↓
The actor changes posture
        ↓
The animator moves the eyes smoothly
        ↓
The renderer draws the result
```

The state machine is the stage manager. It decides which scene is currently active, but it does not draw the scene.

---

## 3. State Categories

The framework separates three concepts.

### 3.1 Application State

What MhaiBot is currently doing:

```cpp
enum class EyeActivity {
    Booting,
    Idle,
    Listening,
    Thinking,
    Speaking,
    Sleeping,
    Waking,
    Error
};
```

### 3.2 Emotion

How the eyes should feel:

```cpp
enum class EyeEmotion {
    Neutral,
    Happy,
    Sad,
    Angry,
    Surprised,
    Focused,
    Sleepy
};
```

### 3.3 Transient Animation

A short animation that can occur while an activity and emotion remain active:

```cpp
enum class EyeTransient {
    None,
    Blink,
    DoubleBlink,
    Glance,
    TouchReaction
};
```

These dimensions are separate. For example:

```text
Activity = Listening
Emotion  = Focused
Transient = Blink
```

---

## 4. Primary State Machine

```text
                     +---------+
                     | Booting |
                     +----+----+
                          |
                          v
                     +----+----+
                     |  Idle   |
                     +----+----+
                          |
         +----------------+----------------+
         |                |                |
         v                v                v
    +----+-----+     +----+----+      +----+----+
    | Listening |     | Thinking |      | Speaking |
    +----+-----+     +----+----+      +----+----+
         |                |                |
         +----------------+----------------+
                          |
                          v
                     +----+----+
                     |  Idle   |
                     +----+----+
                          |
                          v
                     +----+----+
                     | Sleeping|
                     +----+----+
                          |
                          v
                     +----+----+
                     | Waking  |
                     +----+----+
                          |
                          v
                       Idle

Error may interrupt any state.
```

---

## 5. State Responsibilities

| State | Eye behavior | Allowed transient animations |
|---|---|---|
| Booting | Neutral startup pose; no idle randomness | Optional startup blink |
| Idle | Neutral or current emotion; low-amplitude movement | Blink, double blink, glance |
| Listening | Attentive pose; mostly centered gaze | Blink, subtle gaze response |
| Thinking | Focused pose; small deliberate gaze offset | Blink, controlled glance |
| Speaking | Expressive pose driven by speech activity | Blink only when it does not conflict |
| Sleeping | Eyes closed; animation controllers suppressed | None |
| Waking | Controlled opening transition | None until wake completes |
| Error | High-priority error expression | Optional fixed warning blink |

---

## 6. Allowed Transitions

| From | To | Trigger |
|---|---|---|
| Booting | Idle | Display and interaction model ready |
| Idle | Listening | Voice session starts |
| Listening | Thinking | User input captured |
| Thinking | Speaking | Response playback starts |
| Speaking | Idle | Playback completes |
| Any active state | Sleeping | Sleep request or inactivity policy |
| Sleeping | Waking | Wake event |
| Waking | Idle | Wake animation completes |
| Any state | Error | Critical display/system error |
| Error | Previous safe state or Idle | Error cleared |

Invalid transitions should be rejected or normalized by the interaction layer. The renderer must not decide transitions.

---

## 7. Priority Rules

Recommended priority, highest first:

```text
Error
  >
Sleep / Wake transition
  >
Direct user interaction
  >
Speaking / Listening / Thinking
  >
Emotion transition
  >
Idle animation
```

### Example

If a random idle glance is running and MhaiBot begins listening:

1. Cancel or blend out the idle glance.
2. Preserve the current geometric frame as the transition start.
3. Transition to the Listening pose.
4. Keep auto-blink enabled unless the transition explicitly suppresses it.

---

## 8. Blink Sub-State Machine

```text
Idle
  ↓ timer/manual trigger
Closing
  ↓ openness reaches 0
Closed
  ↓ hold duration completes
Opening
  ↓ openness reaches target
Idle
```

Recommended initial timing ranges:

| Phase | Suggested duration |
|---|---:|
| Closing | 70–100 ms |
| Closed | 40–80 ms |
| Opening | 90–130 ms |

The exact numbers remain tuning parameters and require hardware validation.

Blink must be suppressed during:

- Sleeping
- Critical wake transition
- Any animation that already controls full eye openness
- Error states that require a fixed pose

---

## 9. Sleep/Wake State Machine

```text
Awake
  ↓ sleep requested
ClosingForSleep
  ↓ eyes fully closed
Sleeping
  ↓ wake requested
OpeningFromSleep
  ↓ eyes reach awake target
Awake
```

Sleep/wake is not the same as a normal blink:

- It lasts longer.
- It has higher priority.
- It suppresses idle and blink controllers.
- It may coordinate with backlight, audio, or servo systems later.

---

## 10. Error Handling

The state machine should handle invalid inputs conservatively.

Examples:

- Unknown activity → fall back to `Idle`
- Unknown emotion → fall back to `Neutral`
- Negative duration → clamp to zero
- Non-finite geometry value → retain previous valid frame
- Conflicting high-priority transitions → prefer Error, then Sleep/Wake

No state transition should allocate memory or block the UI task.

---

## 11. Thread Ownership

Recommended ownership:

- Interaction model publishes `EyeIntent`.
- One display/UI owner consumes the latest intent.
- `EyeAnimator::Update()` runs from the existing display update context.
- LVGL objects are mutated only from the LVGL-safe context.
- Other tasks must not call LVGL directly.

Cross-task communication should use a small immutable snapshot or mailbox, not shared mutable animation internals.

---

## 12. Acceptance Criteria

The state-machine implementation is complete when:

- All documented primary transitions work.
- Error can interrupt every normal state.
- Sleep/wake suppresses blink and idle behavior.
- Idle animations never override user-interaction states.
- The renderer contains no state-transition logic.
- Rapid state changes do not produce invalid geometry or visible jumps.
- No new blocking call or dynamic allocation occurs in the animation update path.
