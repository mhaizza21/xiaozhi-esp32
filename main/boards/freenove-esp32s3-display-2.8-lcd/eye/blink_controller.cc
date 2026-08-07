#include "blink_controller.h"

#include "eye_animator.h"

namespace {
// Within the 07 §5.1 / §5.2 ranges; single fixed values keep timing
// deterministic for host tests. Exact numbers are a hardware tuning target.
constexpr uint32_t kClosingMs = 90;
constexpr uint32_t kClosedMs = 60;
constexpr uint32_t kOpeningMs = 110;
constexpr uint32_t kDoubleBlinkGapMs = 120;
constexpr uint32_t kAutoBlinkMinMs = 2500;
constexpr uint32_t kAutoBlinkMaxMs = 6000;
}  // namespace

BlinkController::BlinkController(uint32_t rng_seed)
    : rng_state_(rng_seed != 0 ? rng_seed : kDefaultRngSeed) {
    auto_blink_interval_ms_ = NextAutoBlinkIntervalMs();
}

void BlinkController::SetAllowed(bool allowed) {
    allowed_ = allowed;
}

void BlinkController::Trigger(EyeTransient kind) {
    if (!allowed_ || phase_ != Phase::kIdle) {
        return;
    }
    if (kind != EyeTransient::Blink && kind != EyeTransient::DoubleBlink) {
        return;
    }
    BeginBlink();
    double_blink_pending_ = (kind == EyeTransient::DoubleBlink);
}

void BlinkController::Update(uint32_t delta_ms) {
    if (phase_ == Phase::kIdle) {
        openness_multiplier_ = 1.0f;
        if (allowed_) {
            auto_blink_elapsed_ms_ += delta_ms;
            if (auto_blink_elapsed_ms_ >= auto_blink_interval_ms_) {
                auto_blink_elapsed_ms_ = 0;
                auto_blink_interval_ms_ = NextAutoBlinkIntervalMs();
                BeginBlink();
            }
        }
        return;
    }

    if (phase_ == Phase::kGap) {
        phase_elapsed_ms_ += delta_ms;
        if (phase_elapsed_ms_ >= kDoubleBlinkGapMs) {
            BeginBlink();
        }
        return;
    }

    phase_elapsed_ms_ += delta_ms;
    switch (phase_) {
        case Phase::kClosing: {
            const uint16_t progress = EyeAnimator::ClampProgress(phase_elapsed_ms_, kClosingMs);
            openness_multiplier_ = 1.0f - (static_cast<float>(progress) / 1000.0f);
            if (phase_elapsed_ms_ >= kClosingMs) {
                phase_ = Phase::kClosed;
                phase_elapsed_ms_ = 0;
                openness_multiplier_ = 0.0f;
            }
            break;
        }
        case Phase::kClosed: {
            openness_multiplier_ = 0.0f;
            if (phase_elapsed_ms_ >= kClosedMs) {
                phase_ = Phase::kOpening;
                phase_elapsed_ms_ = 0;
            }
            break;
        }
        case Phase::kOpening: {
            const uint16_t progress = EyeAnimator::ClampProgress(phase_elapsed_ms_, kOpeningMs);
            openness_multiplier_ = static_cast<float>(progress) / 1000.0f;
            if (phase_elapsed_ms_ >= kOpeningMs) {
                openness_multiplier_ = 1.0f;
                if (double_blink_pending_) {
                    double_blink_pending_ = false;
                    phase_ = Phase::kGap;
                    phase_elapsed_ms_ = 0;
                } else {
                    phase_ = Phase::kIdle;
                    phase_elapsed_ms_ = 0;
                }
            }
            break;
        }
        default:
            break;
    }
}

void BlinkController::BeginBlink() {
    phase_ = Phase::kClosing;
    phase_elapsed_ms_ = 0;
    openness_multiplier_ = 1.0f;
}

uint32_t BlinkController::NextAutoBlinkIntervalMs() {
    // xorshift32; deterministic and reproducible for a fixed seed (07 §10).
    rng_state_ ^= rng_state_ << 13;
    rng_state_ ^= rng_state_ >> 17;
    rng_state_ ^= rng_state_ << 5;
    constexpr uint32_t kSpan = kAutoBlinkMaxMs - kAutoBlinkMinMs;
    return kAutoBlinkMinMs + (rng_state_ % (kSpan + 1));
}
