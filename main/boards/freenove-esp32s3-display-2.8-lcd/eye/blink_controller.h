#ifndef MHAIBOT_BLINK_CONTROLLER_H
#define MHAIBOT_BLINK_CONTROLLER_H

#include "eye_intent.h"

#include <cstdint>

// LVGL-free blink controller (02 / 05 §8 / 07 §5). Owns blink timing and
// suppression only; the resulting openness_multiplier() is applied to
// height by the shared post-compose stage (ADR-004) — BlinkController never
// touches EyeFrame or LVGL directly.
//
// Deliberate additive behavior (09 Slice 4): legacy MhaiBotFaceV2 has no
// blink today. Auto-blink interval is randomized per event (07 §10), not
// per frame, via a small seeded PRNG so tests are reproducible.
class BlinkController {
public:
    explicit BlinkController(uint32_t rng_seed = kDefaultRngSeed);

    // Suppress (false) or permit (true) both auto-blink scheduling and
    // explicit Trigger(); an in-progress blink still finishes naturally
    // rather than snapping back open (05 §8 / 07 §5.3).
    void SetAllowed(bool allowed);

    // Starts a Blink or DoubleBlink immediately if idle and allowed;
    // ignored otherwise (including while a blink is already in progress).
    void Trigger(EyeTransient kind);

    void Update(uint32_t delta_ms);

    // 0 = fully closed, 1 = fully open (ADR-004 composition multiplier).
    float openness_multiplier() const { return openness_multiplier_; }

private:
    enum class Phase { kIdle, kClosing, kClosed, kOpening, kGap };

    static constexpr uint32_t kDefaultRngSeed = 0x9E3779B9u;

    void BeginBlink();
    uint32_t NextAutoBlinkIntervalMs();

    bool allowed_ = true;
    Phase phase_ = Phase::kIdle;
    uint32_t phase_elapsed_ms_ = 0;
    uint32_t auto_blink_elapsed_ms_ = 0;
    uint32_t auto_blink_interval_ms_;
    bool double_blink_pending_ = false;
    float openness_multiplier_ = 1.0f;
    uint32_t rng_state_;
};

#endif  // MHAIBOT_BLINK_CONTROLLER_H
