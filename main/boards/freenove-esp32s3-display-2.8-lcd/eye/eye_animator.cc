#include "eye_animator.h"

void EyeAnimator::Reset(const EyeFrame& frame) {
    frame_ = frame;
}

void EyeAnimator::Update(uint32_t delta_ms) {
    // Slice 2: pass-through. No controller/target inputs exist yet, so the
    // frame set by Reset() is held as-is; delta_ms is accepted (time-based
    // API per 06 §7) but unused until targets land in later slices.
    (void)delta_ms;
}

uint16_t EyeAnimator::ClampProgress(uint32_t elapsed_ms, uint32_t duration_ms) {
    if (duration_ms == 0 || elapsed_ms >= duration_ms) {
        return 1000;
    }
    return static_cast<uint16_t>((elapsed_ms * 1000U) / duration_ms);
}

int EyeAnimator::LerpInt(int from, int to, uint16_t progress_per_mille) {
    return from + ((to - from) * static_cast<int>(progress_per_mille)) / 1000;
}
