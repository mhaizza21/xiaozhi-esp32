#ifndef MHAIBOT_EYE_ANIMATOR_H
#define MHAIBOT_EYE_ANIMATOR_H

#include "eye_frame.h"

#include <cstdint>

// LVGL-free animator skeleton (02 / 06 §4.4). Slice 2: pass-through only —
// Update() holds whatever frame Reset() set; there are no controller/target
// inputs yet, so no interpolation happens. Later slices add
// SetTargetFromControllers and time-based composition (07) without
// requiring LVGL.
class EyeAnimator {
public:
    void Reset(const EyeFrame& frame);
    void Update(uint32_t delta_ms);
    const EyeFrame& frame() const { return frame_; }

    // Shared lerp helpers extracted from MhaiBotFaceV2 (kept LVGL-free).
    // Legacy keeps its own copies until the Slice 8 transient migration;
    // these are the animator's future callees, not a deletion.
    static uint16_t ClampProgress(uint32_t elapsed_ms, uint32_t duration_ms);
    static int LerpInt(int from, int to, uint16_t progress_per_mille);

private:
    EyeFrame frame_{};
};

#endif  // MHAIBOT_EYE_ANIMATOR_H
