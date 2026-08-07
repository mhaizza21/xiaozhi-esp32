#include "eye/eye_frame.h"
#include "eye/eye_post_compose.h"
#include "eye/idle_controller.h"

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
    // Disabled from the start: offset stays exactly (0,0) for a long
    // duration (well beyond the 07 §6.2 max 7000ms interval) — no
    // per-frame movement, no event ever starts.
    {
        IdleController idle;
        idle.SetEnabled(false);
        for (int i = 0; i < 20000; ++i) {
            idle.Update(1);
            assert(idle.look_offset_x() == 0.0f);
            assert(idle.look_offset_y() == 0.0f);
        }
    }

    // Enabled (default): within the documented max interval bound, an
    // event must start, and the offset must stay within the 07 §6.1 Micro
    // Gaze range at every sampled step. Deterministic given the fixed
    // default seed.
    {
        IdleController idle;
        bool saw_offset = false;
        for (int i = 0; i < 7000; ++i) {
            idle.Update(1);
            const float x = idle.look_offset_x();
            const float y = idle.look_offset_y();
            assert(x >= -0.15f && x <= 0.15f);
            assert(y >= -0.08f && y <= 0.08f);
            if (x != 0.0f || y != 0.0f) {
                saw_offset = true;
            }
        }
        assert(saw_offset);
    }

    // Determinism: two controllers built with the same (default) seed
    // produce an identical trajectory (07 §10 reproducibility).
    {
        IdleController a;
        IdleController b;
        for (int i = 0; i < 3000; ++i) {
            a.Update(5);
            b.Update(5);
            assert(a.look_offset_x() == b.look_offset_x());
            assert(a.look_offset_y() == b.look_offset_y());
        }
    }

    // SetEnabled(false) mid-event forces an early return to center rather
    // than letting the event finish or freezing off-axis.
    {
        IdleController idle;
        int steps = 0;
        while (idle.look_offset_x() == 0.0f && idle.look_offset_y() == 0.0f && steps < 7000) {
            idle.Update(1);
            ++steps;
        }
        assert(steps < 7000);  // an event must have started

        idle.SetEnabled(false);
        // Return duration is bounded (07 §8: 150-400ms); 1000ms is ample
        // margin.
        for (int i = 0; i < 1000; ++i) {
            idle.Update(1);
        }
        assert(idle.look_offset_x() == 0.0f);
        assert(idle.look_offset_y() == 0.0f);

        // Must stay at center indefinitely while disabled — no new event.
        for (int i = 0; i < 10000; ++i) {
            idle.Update(1);
            assert(idle.look_offset_x() == 0.0f);
            assert(idle.look_offset_y() == 0.0f);
        }
    }

    // Shared post-compose stage: ApplyIdleGaze shifts both eyes identically
    // by the documented scale, leaves size/radius untouched, and precedes
    // blink in composition order (07 §9) — verified structurally here by
    // confirming it operates on center_x/center_y only.
    {
        const EyeFrame frame = MakeFrame();

        const EyeFrame centered = ApplyIdleGaze(frame, 0.0f, 0.0f);
        assert(centered.left.center_x == frame.left.center_x);
        assert(centered.left.center_y == frame.left.center_y);

        const EyeFrame shifted = ApplyIdleGaze(frame, 0.15f, -0.08f);
        assert(shifted.left.center_x == frame.left.center_x + 0.15f * 20.0f);
        assert(shifted.right.center_x == frame.right.center_x + 0.15f * 20.0f);
        assert(shifted.left.center_y == frame.left.center_y + (-0.08f) * 25.0f);
        assert(shifted.right.center_y == frame.right.center_y + (-0.08f) * 25.0f);
        assert(shifted.left.width == frame.left.width);
        assert(shifted.left.height == frame.left.height);
        assert(shifted.left.corner_radius == frame.left.corner_radius);
        // Gap between eyes (right.center_x - left.center_x) is preserved —
        // both eyes shift together, so no new overlap risk (07 §11).
        assert((shifted.right.center_x - shifted.left.center_x) ==
               (frame.right.center_x - frame.left.center_x));

        // Non-finite / out-of-range offsets are clamped defensively.
        const EyeFrame nan_guard =
            ApplyIdleGaze(frame, std::numeric_limits<float>::quiet_NaN(), 0.5f);
        assert(nan_guard.left.center_x == frame.left.center_x);

        const EyeFrame clamp_high = ApplyIdleGaze(frame, 5.0f, 0.0f);
        assert(clamp_high.left.center_x == frame.left.center_x + 1.0f * 20.0f);

        const EyeFrame clamp_low = ApplyIdleGaze(frame, -5.0f, 0.0f);
        assert(clamp_low.left.center_x == frame.left.center_x - 1.0f * 20.0f);
    }

    return 0;
}
