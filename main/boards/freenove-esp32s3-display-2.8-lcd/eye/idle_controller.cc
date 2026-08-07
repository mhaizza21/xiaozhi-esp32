#include "idle_controller.h"

#include "eye_animator.h"

namespace {
// Fixed representative values within documented ranges (same philosophy as
// BlinkController: the *interval*/*target* are randomized per event, the
// phase durations are fixed tuning constants pending hardware validation).
constexpr uint32_t kMoveMs = 250;    // 07 §8 "Gaze movement": 150-400ms
constexpr uint32_t kHoldMs = 600;    // 07 §6.1 Micro Gaze duration: 300-900ms
constexpr uint32_t kReturnMs = 250;  // 07 §8 "Gaze movement": 150-400ms
constexpr uint32_t kEventIntervalMinMs = 2000;  // 07 §6.2: 2-7 seconds
constexpr uint32_t kEventIntervalMaxMs = 7000;
constexpr float kMaxLookX = 0.15f;  // 07 §6.1
constexpr float kMaxLookY = 0.08f;  // 07 §6.1
}  // namespace

IdleController::IdleController(uint32_t rng_seed)
    : rng_state_(rng_seed != 0 ? rng_seed : kDefaultRngSeed) {
    wait_interval_ms_ = NextEventIntervalMs();
}

void IdleController::SetEnabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (!enabled_ && phase_ != Phase::kWaiting) {
        // Force an early return-to-center instead of continuing whatever
        // phase was active, rather than letting it finish naturally
        // (unlike BlinkController) — a non-centered idle offset would
        // conflict with interaction poses (07 §7.2 "gaze near center").
        origin_offset_x_ = look_offset_x_;
        origin_offset_y_ = look_offset_y_;
        target_offset_x_ = 0.0f;
        target_offset_y_ = 0.0f;
        phase_ = Phase::kReturning;
        phase_elapsed_ms_ = 0;
    }
}

void IdleController::Update(uint32_t delta_ms) {
    switch (phase_) {
        case Phase::kWaiting: {
            look_offset_x_ = 0.0f;
            look_offset_y_ = 0.0f;
            if (enabled_) {
                wait_elapsed_ms_ += delta_ms;
                if (wait_elapsed_ms_ >= wait_interval_ms_) {
                    wait_elapsed_ms_ = 0;
                    wait_interval_ms_ = NextEventIntervalMs();
                    BeginEvent();
                }
            }
            break;
        }
        case Phase::kMoving: {
            phase_elapsed_ms_ += delta_ms;
            const uint16_t progress = EyeAnimator::ClampProgress(phase_elapsed_ms_, kMoveMs);
            look_offset_x_ = LerpFloat(origin_offset_x_, target_offset_x_, progress);
            look_offset_y_ = LerpFloat(origin_offset_y_, target_offset_y_, progress);
            if (phase_elapsed_ms_ >= kMoveMs) {
                look_offset_x_ = target_offset_x_;
                look_offset_y_ = target_offset_y_;
                phase_ = Phase::kHolding;
                phase_elapsed_ms_ = 0;
            }
            break;
        }
        case Phase::kHolding: {
            phase_elapsed_ms_ += delta_ms;
            if (phase_elapsed_ms_ >= kHoldMs) {
                origin_offset_x_ = look_offset_x_;
                origin_offset_y_ = look_offset_y_;
                target_offset_x_ = 0.0f;
                target_offset_y_ = 0.0f;
                phase_ = Phase::kReturning;
                phase_elapsed_ms_ = 0;
            }
            break;
        }
        case Phase::kReturning: {
            phase_elapsed_ms_ += delta_ms;
            const uint16_t progress = EyeAnimator::ClampProgress(phase_elapsed_ms_, kReturnMs);
            look_offset_x_ = LerpFloat(origin_offset_x_, target_offset_x_, progress);
            look_offset_y_ = LerpFloat(origin_offset_y_, target_offset_y_, progress);
            if (phase_elapsed_ms_ >= kReturnMs) {
                look_offset_x_ = 0.0f;
                look_offset_y_ = 0.0f;
                phase_ = Phase::kWaiting;
                phase_elapsed_ms_ = 0;
                wait_elapsed_ms_ = 0;
            }
            break;
        }
    }
}

void IdleController::BeginEvent() {
    origin_offset_x_ = 0.0f;
    origin_offset_y_ = 0.0f;
    target_offset_x_ = NextOffsetComponent(kMaxLookX);
    target_offset_y_ = NextOffsetComponent(kMaxLookY);
    phase_ = Phase::kMoving;
    phase_elapsed_ms_ = 0;
}

uint32_t IdleController::NextEventIntervalMs() {
    // xorshift32; deterministic and reproducible for a fixed seed (07 §10).
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    constexpr uint32_t kSpan = kEventIntervalMaxMs - kEventIntervalMinMs;
    return kEventIntervalMinMs + (rng_state_ % (kSpan + 1));
}

float IdleController::NextOffsetComponent(float max_abs) {
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    // [0, 2000] -> [-1.0, 1.0] in 0.001 steps, then scale to the requested
    // bound.
    const float unit = (static_cast<float>(rng_state_ % 2001) / 1000.0f) - 1.0f;
    return unit * max_abs;
}

float IdleController::LerpFloat(float from, float to, uint16_t progress_per_mille) {
    return from + (to - from) * (static_cast<float>(progress_per_mille) / 1000.0f);
}
