#include "eye_post_compose.h"

#include <algorithm>
#include <cmath>

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
