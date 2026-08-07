#include "eye_post_compose.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kGazeScaleXPx = 20.0f;
constexpr float kGazeScaleYPx = 25.0f;
}  // namespace

EyeFrame ApplyBlinkOpenness(const EyeFrame& canonical, float openness_multiplier) {
    const float clamped =
        std::isfinite(openness_multiplier) ? std::clamp(openness_multiplier, 0.0f, 1.0f) : 1.0f;
    if (clamped >= 1.0f) {
        return canonical;
    }

    EyeFrame frame = canonical;
    frame.left.height *= clamped;
    frame.right.height *= clamped;
    return frame;
}

EyeFrame ApplyIdleGaze(const EyeFrame& canonical, float look_offset_x, float look_offset_y) {
    const float safe_x =
        std::isfinite(look_offset_x) ? std::clamp(look_offset_x, -1.0f, 1.0f) : 0.0f;
    const float safe_y =
        std::isfinite(look_offset_y) ? std::clamp(look_offset_y, -1.0f, 1.0f) : 0.0f;
    if (safe_x == 0.0f && safe_y == 0.0f) {
        return canonical;
    }

    const float dx = safe_x * kGazeScaleXPx;
    const float dy = safe_y * kGazeScaleYPx;

    EyeFrame frame = canonical;
    frame.left.center_x += dx;
    frame.left.center_y += dy;
    frame.right.center_x += dx;
    frame.right.center_y += dy;
    return frame;
}
