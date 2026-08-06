# Eye Animation Rendering Pipeline

**Status:** Draft  
**Scope:** MhaiBot LVGL display integration  
**Implementation status:** Planning only

---

## 1. Purpose

This document defines how eye-animation data moves from the MhaiBot interaction model to the LCD.

The goal is to keep:

- interaction logic independent from animation,
- animation independent from LVGL,
- rendering deterministic and lightweight,
- all LVGL access inside the correct execution context.

---

## 2. ELI10

Think of a restaurant:

```text
Customer order
    ↓
Recipe decision
    ↓
Food preparation
    ↓
Plating
    ↓
Serving
```

In MhaiBot:

```text
Interaction state
    ↓
EyeIntent
    ↓
EyeAnimator
    ↓
EyeFrame
    ↓
LVGLEyeRenderer
    ↓
LCD
```

The waiter should not cook, and the cook should not decide what the customer ordered.

---

## 3. Pipeline Overview

```text
+---------------------------+
| MhaiBot Interaction Model |
+-------------+-------------+
              |
              | EyeIntent snapshot
              v
+-------------+-------------+
| Eye Animation Coordinator |
+-------------+-------------+
              |
              | controller inputs
              v
+-------------+-------------+
| EyeAnimator::Update(dt)    |
+-------------+-------------+
              |
              | EyeFrame
              v
+-------------+-------------+
| LVGLEyeRenderer::Render()  |
+-------------+-------------+
              |
              | LVGL object updates
              v
+-------------+-------------+
| LVGL invalidation/refresh  |
+-------------+-------------+
              |
              v
             LCD
```

---

## 4. Pipeline Stages

### 4.1 Interaction Model

Inputs may include:

- assistant state,
- current emotion,
- voice-session state,
- sleep/wake request,
- error state,
- future touch or tracking input.

Output:

```cpp
struct EyeIntent {
    EyeActivity activity;
    EyeEmotion emotion;
    float look_x;
    float look_y;
    bool blink_allowed;
};
```

The interaction model must not know eye geometry or LVGL object details.

### 4.2 Animation Coordinator

Responsibilities:

- Accept the latest `EyeIntent`.
- Apply priority rules.
- Trigger high-level transitions.
- Forward stable input to animation controllers.

It should not draw and should not create LVGL objects.

### 4.3 Controllers

Initial controller set:

- EmotionController
- BlinkController
- IdleController

Each controller contributes a limited, documented part of the final pose.

Example:

```text
EmotionController → base size and shape
BlinkController   → openness multiplier
IdleController    → small gaze/position offsets
```

### 4.4 EyeAnimator

`EyeAnimator` composes controller outputs and interpolates from the current pose to the target pose.

Input:

- delta time,
- normalized intent,
- controller state.

Output:

```cpp
struct EyeFrame {
    EyeGeometry left;
    EyeGeometry right;
    float opacity;
};
```

`EyeAnimator` must not include or call LVGL.

### 4.5 LVGL Renderer

The renderer owns references to pre-created LVGL objects.

It may:

- update position,
- update size,
- update radius,
- update rotation if supported by the chosen object strategy,
- update opacity,
- invalidate changed objects.

It must not:

- decide emotions,
- schedule blinks,
- own assistant state,
- allocate a new object every frame.

---

## 5. Suggested Render Data Model

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

Before passing values to LVGL:

- clamp dimensions to display bounds,
- reject non-finite values,
- convert normalized values to pixels,
- round consistently to avoid one-pixel jitter.

---

## 6. LVGL Object Strategy

Recommended initial implementation:

- Create two persistent eye objects once.
- Use rounded rectangles for the minimal-eye style.
- Update object geometry instead of recreating objects.
- Keep background and eye layers separate.
- Reuse existing display container when possible.

Avoid:

```cpp
// Do not do this every frame.
lv_obj_t* eye = lv_obj_create(parent);
```

Prefer:

```cpp
lv_obj_set_pos(left_eye_, x, y);
lv_obj_set_size(left_eye_, width, height);
lv_obj_set_style_radius(left_eye_, radius, 0);
```

Exact APIs must be checked against the LVGL version currently used by the repository.

---

## 7. Timing Model

The animation system should use elapsed time rather than assuming a perfect fixed frame rate.

```cpp
animator.Update(delta_ms);
```

Suggested target:

- 30 FPS minimum for normal operation
- 60 FPS only if existing display performance supports it
- animation remains correct when frames are delayed

Interpolation should be time-based:

```text
progress = elapsed / duration
```

not frame-count-based:

```text
progress += fixed_amount_per_frame
```

---

## 8. Thread and Task Ownership

Critical rule:

> Only the LVGL-safe owner may mutate LVGL objects.

Recommended flow:

```text
Audio/network/application tasks
           |
           | publish intent only
           v
     UI/display context
           |
           | animator update
           | renderer update
           v
          LVGL
```

Do not call the renderer from:

- audio tasks,
- network callbacks,
- servo tasks,
- ISR context.

---

## 9. Performance Budget

The initial renderer should:

- avoid per-frame heap allocation,
- avoid file I/O,
- avoid logging every frame,
- avoid full-screen redraw where object invalidation is sufficient,
- avoid trigonometric work unless required and measured,
- preserve watchdog-safe update behavior.

Measurements required during hardware validation:

- frame interval,
- free heap before/after,
- largest free block,
- CPU utilization if available,
- dropped/janky frames,
- watchdog and reset logs.

---

## 10. Failure Handling

| Failure | Required response |
|---|---|
| Invalid `EyeIntent` | Normalize to safe defaults |
| Non-finite frame value | Keep prior valid value |
| Geometry outside bounds | Clamp to display |
| Renderer not initialized | Skip render and report once |
| LVGL object missing | Fail safely without recreation loop |
| Excessive update delay | Advance animation using elapsed time |

---

## 11. Integration Sequence

1. Inspect the current MhaiBot display and interaction model.
2. Identify existing LVGL eye objects and ownership.
3. Introduce `EyeFrame` without changing visible behavior.
4. Implement renderer adapter for the existing eye style.
5. Introduce `EyeAnimator` with neutral static output.
6. Add blink.
7. Add idle movement.
8. Add emotion mapping.
9. Remove replaced legacy animation logic only after hardware validation.

---

## 12. Acceptance Criteria

- Renderer receives only `EyeFrame`.
- Animator has no LVGL dependency.
- LVGL objects are created once and reused.
- No per-frame dynamic allocation.
- All LVGL mutation occurs in the correct context.
- Existing display functions continue to work.
- Sleep/wake and status UI retain correct priority.
- Hardware testing shows no watchdog reset or visible jitter.
