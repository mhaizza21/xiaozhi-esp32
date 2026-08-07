#ifndef MHAIBOT_EMOTION_CONTROLLER_H
#define MHAIBOT_EMOTION_CONTROLLER_H

#include "eye_frame.h"
#include "eye_intent.h"

#include <cstdint>

// LVGL-free emotion base-pose controller (02 / 09 Slice 6). Owns only the
// canonical EyeEmotion -> base EyeFrame lookup ("activity-neutral"
// baselines per 07 §4) — not activity-specific geometry tweaks
// (Listening/Thinking attention etc. — Slice 7), not transient overlays
// (pet/startle/groggy — Slice 8), not DeviceState/mailbox policy (Slice 3),
// not LVGL, not blink/idle scheduling.
//
// Mirrors MhaiBotFaceV2::ResolveBasePose (an instant target lookup) rather
// than ResolveRenderedPose (which also blends transients/transitions over
// time) — Update(delta_ms) exists to match the documented dt-based
// controller API and is reserved for future transition timing (inventory
// T5, migrated in Slice 8); it is a documented no-op today since there is
// nothing to animate at this layer yet.
class EmotionController {
public:
    EmotionController();

    void SetEmotion(EyeEmotion emotion);
    void Update(uint32_t delta_ms);

    const EyeFrame& frame() const { return frame_; }
    EyeEmotion emotion() const { return emotion_; }

    // Slice 8: exposed publicly (pure visibility change, no logic change)
    // so EyeAnimationCoordinator can query fixed emotion bases (e.g. Happy
    // for Petting, Sleepy/Neutral for GroggyWake) without owning a second
    // EmotionController instance — consuming this static table, not
    // duplicating it (09 Slice 6/7/8 ownership discipline).
    static EyeFrame BasePose(EyeEmotion emotion);

private:
    EyeEmotion emotion_ = EyeEmotion::Neutral;
    EyeFrame frame_{};
};

#endif  // MHAIBOT_EMOTION_CONTROLLER_H
