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

#endif  // MHAIBOT_EYE_POST_COMPOSE_H
