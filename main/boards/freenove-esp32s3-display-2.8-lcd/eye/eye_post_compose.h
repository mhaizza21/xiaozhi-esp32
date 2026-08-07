#ifndef MHAIBOT_EYE_POST_COMPOSE_H
#define MHAIBOT_EYE_POST_COMPOSE_H

#include "eye_frame.h"

// Shared post-compose stage (07 §9, ADR-004). Applied identically regardless
// of which pixel path produced the canonical frame (legacy-primary today;
// mailbox/animator-primary after Slice 11) so visible blink is
// path-independent — this is why the helper is a free function rather than
// private to EyeAnimator. Slice 4 adds blink openness only; Slice 5 adds
// idle gaze/glance in the same stage.
//
// Scales height only, around the existing center_y, so the eye closes
// symmetrically without shifting position. EyeFrame gains no openness
// field (ADR-004) — the multiplier is consumed here, not carried forward.
// Non-finite or out-of-[0,1] multipliers are clamped defensively (06 §10 /
// 07 §11); canonical is returned unchanged for a fully-open (>=1) result.
EyeFrame ApplyBlinkOpenness(const EyeFrame& canonical, float openness_multiplier);

// Shifts both eyes' center_x/center_y by a normalized gaze offset (07 §3:
// -1..1), converted to pixels by a fixed scale constant — a tuning target
// pending hardware validation (07 §1), chosen so the Slice 5 micro-gaze
// range (±0.15 x, ±0.08 y — 07 §6.1) produces a visible-but-subtle few
// pixels of motion rather than rounding away to nothing. Both eyes shift
// identically (a single shared gaze direction), so the existing eye_gap
// spacing is preserved and no new overlap risk is introduced (07 §11).
//
// Composition order (07 §9): call this BEFORE ApplyBlinkOpenness — idle
// gaze composes before the blink multiplier. Non-finite or out-of-[-1,1]
// offsets are clamped defensively; canonical is returned unchanged for a
// zero offset.
EyeFrame ApplyIdleGaze(const EyeFrame& canonical, float look_offset_x, float look_offset_y);

#endif  // MHAIBOT_EYE_POST_COMPOSE_H
