#include "eye/blink_controller.h"
#include "eye/eye_frame.h"
#include "eye/eye_intent.h"
#include "eye/eye_post_compose.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace {

EyeFrame MakeFrame() {
    EyeFrame frame{};
    frame.left = {10.0f, 50.0f, 40.0f, 60.0f, 8.0f, 0.0f};
    frame.right = {90.0f, 50.0f, 40.0f, 60.0f, 8.0f, 0.0f};
    frame.opacity = 1.0f;
    return frame;
}

}  // namespace

int main() {
    // Explicit Trigger(Blink): openness must stay in [0,1], reach exactly 0
    // during the closed phase, and return to 1 by the end of the documented
    // 07 §5.1 duration budget (90 + 60 + 110 = 260ms).
    {
        BlinkController blink;
        assert(blink.openness_multiplier() == 1.0f);
        blink.Trigger(EyeTransient::Blink);
        assert(blink.openness_multiplier() == 1.0f);  // not yet advanced

        float min_seen = 1.0f;
        for (int i = 0; i < 260; ++i) {
            blink.Update(1);
            const float m = blink.openness_multiplier();
            assert(m >= 0.0f && m <= 1.0f);
            min_seen = std::min(min_seen, m);
        }
        assert(min_seen == 0.0f);
        assert(blink.openness_multiplier() == 1.0f);
    }

    // SetAllowed(false) suppresses both auto-blink and explicit Trigger, for
    // a duration well beyond the max 07 §5.1 auto-blink interval (6000ms).
    {
        BlinkController blink;
        blink.SetAllowed(false);
        blink.Trigger(EyeTransient::Blink);  // must be ignored
        for (int i = 0; i < 20000; ++i) {
            blink.Update(1);
            assert(blink.openness_multiplier() == 1.0f);
        }
    }

    // Default (allowed) auto-blink must occur within the documented max
    // interval bound; deterministic because the RNG seed is fixed.
    {
        BlinkController blink;
        bool saw_close = false;
        for (int i = 0; i < 6000; ++i) {
            blink.Update(1);
            if (blink.openness_multiplier() < 1.0f) {
                saw_close = true;
                break;
            }
        }
        assert(saw_close);
    }

    // DoubleBlink: exactly two distinct fully-closed samples, separated by a
    // return to fully-open (the 80-160ms gap, 07 §5.2), then settles open.
    {
        BlinkController blink;
        blink.Trigger(EyeTransient::DoubleBlink);
        int zero_count = 0;
        bool was_zero = false;
        for (int i = 0; i < 800; ++i) {
            blink.Update(1);
            const float m = blink.openness_multiplier();
            if (m == 0.0f) {
                if (!was_zero) {
                    ++zero_count;
                }
                was_zero = true;
            } else {
                was_zero = false;
            }
        }
        assert(zero_count == 2);
        assert(blink.openness_multiplier() == 1.0f);
    }

    // Shared post-compose stage (ADR-004): height scales around the
    // existing center_y; width/radius/position untouched; EyeFrame carries
    // no openness field to begin with.
    {
        const EyeFrame frame = MakeFrame();

        const EyeFrame open = ApplyBlinkOpenness(frame, 1.0f);
        assert(open.left.height == frame.left.height);
        assert(open.left.center_y == frame.left.center_y);

        const EyeFrame half = ApplyBlinkOpenness(frame, 0.5f);
        assert(half.left.height == frame.left.height * 0.5f);
        assert(half.right.height == frame.right.height * 0.5f);
        assert(half.left.center_y == frame.left.center_y);
        assert(half.left.width == frame.left.width);
        assert(half.left.corner_radius == frame.left.corner_radius);
        assert(half.left.center_x == frame.left.center_x);

        const EyeFrame closed = ApplyBlinkOpenness(frame, 0.0f);
        assert(closed.left.height == 0.0f);
        assert(closed.right.height == 0.0f);

        // Non-finite / out-of-range multipliers are clamped defensively.
        const EyeFrame nan_guard =
            ApplyBlinkOpenness(frame, std::numeric_limits<float>::quiet_NaN());
        assert(nan_guard.left.height == frame.left.height);

        const EyeFrame clamp_high = ApplyBlinkOpenness(frame, 1.5f);
        assert(clamp_high.left.height == frame.left.height);

        const EyeFrame clamp_low = ApplyBlinkOpenness(frame, -0.5f);
        assert(clamp_low.left.height == 0.0f);
    }

    return 0;
}
