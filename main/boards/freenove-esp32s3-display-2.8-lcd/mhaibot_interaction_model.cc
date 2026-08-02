#include "mhaibot_interaction_model.h"

#include <cstdlib>

namespace {

constexpr uint16_t kPetZoneTop = 32;
constexpr uint16_t kPetZoneBottom = 104;
constexpr uint16_t kPetMinStrokePx = 40;
constexpr uint16_t kPetMaxVerticalDriftPx = 24;
constexpr uint32_t kPetTimeoutMs = 2000;
constexpr uint32_t kPetDurationMs = 3000;
constexpr uint32_t kIdleSleepTimeoutSeconds = 600;
constexpr uint32_t kScreenOffIdleSeconds = 2400;
constexpr uint32_t kGroggyWakeDurationMs = 5000;
constexpr uint8_t kGroggyMinBrightness = 20;
constexpr uint16_t kStartleTapMaxDriftPx = 24;
constexpr uint32_t kStartleTapWindowMs = 700;
constexpr uint32_t kStartleDurationMs = 700;

bool IsPetZoneY(uint16_t y) {
    return y >= kPetZoneTop && y <= kPetZoneBottom;
}

int8_t DirectionForDelta(int16_t delta_x) {
    if (delta_x >= static_cast<int16_t>(kPetMinStrokePx)) {
        return 1;
    }
    if (delta_x <= -static_cast<int16_t>(kPetMinStrokePx)) {
        return -1;
    }
    return 0;
}

}  // namespace

bool MhaiBotPetGestureDetector::Update(bool touched, uint16_t x, uint16_t y,
                                       uint32_t now_ms) {
    if (!touched) {
        Reset();
        return false;
    }

    if (!active_) {
        if (!IsPetZoneY(y)) {
            Reset();
            return false;
        }
        active_ = true;
        start_y_ = y;
        leg_start_x_ = static_cast<int16_t>(x);
        direction_ = 0;
        reversals_ = 0;
        started_ms_ = now_ms;
        return false;
    }

    if (!IsPetZoneY(y) || now_ms - started_ms_ > kPetTimeoutMs ||
        std::abs(static_cast<int>(y) - static_cast<int>(start_y_)) >
            static_cast<int>(kPetMaxVerticalDriftPx)) {
        Reset();
        return false;
    }

    const int16_t delta_x = static_cast<int16_t>(x) - leg_start_x_;
    const int8_t new_direction = DirectionForDelta(delta_x);
    if (new_direction == 0) {
        return false;
    }

    if (direction_ != 0 && new_direction != direction_) {
        ++reversals_;
        if (reversals_ >= 3) {
            Reset();
            return true;
        }
    }

    direction_ = new_direction;
    leg_start_x_ = static_cast<int16_t>(x);
    return false;
}

void MhaiBotPetGestureDetector::Reset() {
    active_ = false;
    start_y_ = 0;
    leg_start_x_ = 0;
    direction_ = 0;
    reversals_ = 0;
    started_ms_ = 0;
}

bool MhaiBotStartleTapDetector::Update(bool touched, uint16_t x, uint16_t y, uint32_t now_ms) {
    const bool rising_edge = touched && !was_touched_;
    was_touched_ = touched;
    if (!rising_edge) {
        return false;
    }

    if (tap_count_ > 0 && now_ms - first_tap_ms_ > kStartleTapWindowMs) {
        tap_count_ = 0;
    }

    if (tap_count_ > 0) {
        const int delta_x = static_cast<int>(x) - static_cast<int>(anchor_x_);
        const int delta_y = static_cast<int>(y) - static_cast<int>(anchor_y_);
        if (std::abs(delta_x) > static_cast<int>(kStartleTapMaxDriftPx) ||
            std::abs(delta_y) > static_cast<int>(kStartleTapMaxDriftPx)) {
            tap_count_ = 0;
        }
    }

    if (tap_count_ == 0) {
        anchor_x_ = x;
        anchor_y_ = y;
        first_tap_ms_ = now_ms;
        tap_count_ = 1;
        return false;
    }

    ++tap_count_;
    if (tap_count_ >= 3) {
        tap_count_ = 0;
        return true;
    }
    return false;
}

void MhaiBotStartleTapDetector::Reset() {
    was_touched_ = false;
    tap_count_ = 0;
    anchor_x_ = 0;
    anchor_y_ = 0;
    first_tap_ms_ = 0;
}

uint16_t MhaiBotStartleTapMaxDriftPx() {
    return kStartleTapMaxDriftPx;
}

uint32_t MhaiBotStartleTapWindowMs() {
    return kStartleTapWindowMs;
}

uint32_t MhaiBotStartleDurationMs() {
    return kStartleDurationMs;
}

uint16_t MhaiBotPetZoneTop() {
    return kPetZoneTop;
}

uint16_t MhaiBotPetZoneBottom() {
    return kPetZoneBottom;
}

uint16_t MhaiBotPetMinStrokePx() {
    return kPetMinStrokePx;
}

uint16_t MhaiBotPetMaxVerticalDriftPx() {
    return kPetMaxVerticalDriftPx;
}

uint32_t MhaiBotPetTimeoutMs() {
    return kPetTimeoutMs;
}

uint32_t MhaiBotPetDurationMs() {
    return kPetDurationMs;
}

uint32_t MhaiBotIdleSleepTimeoutSeconds() {
    return kIdleSleepTimeoutSeconds;
}

uint32_t MhaiBotScreenOffIdleSeconds() {
    return kScreenOffIdleSeconds;
}

uint32_t MhaiBotGroggyWakeDurationMs() {
    return kGroggyWakeDurationMs;
}

std::string_view MhaiBotSleepText(uint32_t elapsed_ms) {
    switch ((elapsed_ms / 500) % 4) {
        case 0:
            return "Z";
        case 1:
            return "Zz";
        case 2:
            return "Zzz";
        default:
            return "";
    }
}

uint16_t MhaiBotGroggyProgressPerMille(uint32_t elapsed_ms) {
    if (elapsed_ms >= kGroggyWakeDurationMs) {
        return 1000;
    }
    return static_cast<uint16_t>((elapsed_ms * 1000U) / kGroggyWakeDurationMs);
}

uint8_t MhaiBotGroggyBrightness(uint32_t elapsed_ms, uint8_t target_brightness) {
    if (target_brightness < kGroggyMinBrightness) {
        target_brightness = kGroggyMinBrightness;
    }
    const uint16_t progress = MhaiBotGroggyProgressPerMille(elapsed_ms);
    const uint16_t span = static_cast<uint16_t>(target_brightness - kGroggyMinBrightness);
    return static_cast<uint8_t>(kGroggyMinBrightness + (span * progress) / 1000U);
}

MhaiBotAlert MhaiBotResolveAlert(bool error_active, bool battery_low) {
    if (error_active) {
        return MhaiBotAlert::kError;
    }
    if (battery_low) {
        return MhaiBotAlert::kBatteryLow;
    }
    return MhaiBotAlert::kNone;
}
