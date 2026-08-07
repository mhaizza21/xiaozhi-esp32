#include "eye/emotion_controller.h"
#include "eye/eye_frame.h"
#include "eye/eye_intent.h"

#include <cassert>
#include <cmath>

namespace {

bool GeometryFinite(const EyeGeometry& g) {
    return std::isfinite(g.center_x) && std::isfinite(g.center_y) && std::isfinite(g.width) &&
           std::isfinite(g.height) && std::isfinite(g.corner_radius) &&
           std::isfinite(g.rotation_degrees);
}

bool GeometryEqual(const EyeGeometry& a, const EyeGeometry& b) {
    return a.center_x == b.center_x && a.center_y == b.center_y && a.width == b.width &&
           a.height == b.height && a.corner_radius == b.corner_radius &&
           a.rotation_degrees == b.rotation_degrees;
}

// Legacy MhaiBotFaceV2::ResolveBasePose numbers for the emotions with a
// direct legacy precedent, computed with the default Config (root_width
// 240, eye_width 52, eye_height 62, eye_gap 42, eye_y 58, corner_radius
// 16), converted to center-based EyeGeometry the same way
// PoseToEyeFrame does (center = origin + size/2). Used to cross-check
// EmotionController's extracted numbers without duplicating any
// production mapping table.
constexpr float kLeftX = 47.0f;
constexpr float kRightX = 141.0f;
constexpr float kEyeWidth = 52.0f;
constexpr float kEyeHeight = 62.0f;
constexpr float kRadius = 16.0f;

}  // namespace

int main() {
    // Default-constructed controller starts at Neutral, matching legacy
    // kNeutral/kRobot2 (left=47, right=141, y=58, width=52, height=62,
    // radius=16).
    {
        EmotionController controller;
        assert(controller.emotion() == EyeEmotion::Neutral);
        const EyeFrame& f = controller.frame();
        assert(f.left.center_x == kLeftX + kEyeWidth / 2.0f);
        assert(f.left.center_y == 58.0f + kEyeHeight / 2.0f);
        assert(f.left.width == kEyeWidth);
        assert(f.left.height == kEyeHeight);
        assert(f.left.corner_radius == kRadius);
        assert(f.right.center_x == kRightX + kEyeWidth / 2.0f);
        assert(f.right.center_y == f.left.center_y);
        assert(f.opacity == 1.0f);
    }

    // Happy: matches legacy kHappy (y+8=66, height=34).
    {
        EmotionController controller;
        controller.SetEmotion(EyeEmotion::Happy);
        const EyeFrame& f = controller.frame();
        assert(f.left.center_y == 66.0f + 34.0f / 2.0f);
        assert(f.left.height == 34.0f);
        assert(f.left.width == kEyeWidth);
    }

    // Focused: matches legacy kConfident (left+4, right-4, y+2=60,
    // height=48) — the activity-neutral member of the Thinking/Listening/
    // Confident trio that all collapse to Focused (ADR-002).
    {
        EmotionController controller;
        controller.SetEmotion(EyeEmotion::Focused);
        const EyeFrame& f = controller.frame();
        assert(f.left.center_x == (kLeftX + 4.0f) + kEyeWidth / 2.0f);
        assert(f.right.center_x == (kRightX - 4.0f) + kEyeWidth / 2.0f);
        assert(f.left.center_y == 60.0f + 48.0f / 2.0f);
        assert(f.left.height == 48.0f);
    }

    // Sleepy: matches legacy kRelaxed/kSleepy (y+13=71, height=24) — NOT
    // the deeper kSleeping closure (y+22, height=8), which is an activity
    // override reserved for Slice 7.
    {
        EmotionController controller;
        controller.SetEmotion(EyeEmotion::Sleepy);
        const EyeFrame& f = controller.frame();
        assert(f.left.center_y == 71.0f + 24.0f / 2.0f);
        assert(f.left.height == 24.0f);
        assert(f.left.height != 8.0f);  // must not be the Sleeping closure
    }

    // Sad/Angry/Surprised: no legacy precedent; verify each is finite,
    // positive-sized, distinct from Neutral, and within the 07 §4 relative
    // ordering implied by the qualitative spec (Sad/Angry more closed than
    // Neutral; Surprised more open than Neutral).
    {
        EmotionController controller;
        const float neutral_height = controller.frame().left.height;

        controller.SetEmotion(EyeEmotion::Sad);
        assert(GeometryFinite(controller.frame().left));
        assert(controller.frame().left.height > 0.0f);
        assert(controller.frame().left.height < neutral_height);

        controller.SetEmotion(EyeEmotion::Angry);
        assert(GeometryFinite(controller.frame().left));
        assert(controller.frame().left.width > 0.0f);
        assert(controller.frame().left.height > 0.0f);
        assert(controller.frame().left.width < kEyeWidth);   // narrower shape (07 §4.4)
        assert(controller.frame().left.height < neutral_height);

        controller.SetEmotion(EyeEmotion::Surprised);
        assert(GeometryFinite(controller.frame().left));
        assert(controller.frame().left.height > neutral_height);  // increased openness (07 §4.5)
        assert(controller.frame().left.width >= kEyeWidth);
    }

    // Every documented EyeEmotion produces a finite, positive-sized,
    // non-overlapping stereo pair (07 §11 safety limits), and opacity is
    // always fully visible (EmotionController does not own opacity
    // transients like petting's shimmer).
    {
        const EyeEmotion all[] = {EyeEmotion::Neutral, EyeEmotion::Happy, EyeEmotion::Sad,
                                   EyeEmotion::Angry,   EyeEmotion::Surprised, EyeEmotion::Focused,
                                   EyeEmotion::Sleepy};
        for (EyeEmotion e : all) {
            EmotionController controller;
            controller.SetEmotion(e);
            const EyeFrame& f = controller.frame();
            assert(GeometryFinite(f.left) && GeometryFinite(f.right));
            assert(f.left.width > 0.0f && f.left.height > 0.0f);
            assert(f.right.width > 0.0f && f.right.height > 0.0f);
            assert(f.left.center_x < f.right.center_x);  // left/right eyes never swap or overlap
            assert(f.opacity == 1.0f);
            assert(controller.emotion() == e);
        }
    }

    // Update(delta_ms) is a documented no-op in Slice 6 (base pose is an
    // instant lookup, not an interpolation) — must hold across zero and
    // extreme dt values, with no per-frame heap allocation implied by the
    // API (nothing here to allocate).
    {
        EmotionController controller;
        controller.SetEmotion(EyeEmotion::Happy);
        const EyeFrame before = controller.frame();
        controller.Update(0);
        assert(GeometryEqual(controller.frame().left, before.left));
        controller.Update(4294967295u);  // extreme dt (uint32_t max)
        assert(GeometryEqual(controller.frame().left, before.left));
        assert(controller.emotion() == EyeEmotion::Happy);
    }

    // SetEmotion is deterministic and idempotent: repeated calls with the
    // same value do not perturb the frame, and switching away and back
    // reproduces the exact same geometry (pure function of EyeEmotion).
    {
        EmotionController a;
        a.SetEmotion(EyeEmotion::Angry);
        const EyeFrame angry_first = a.frame();
        a.SetEmotion(EyeEmotion::Angry);
        assert(GeometryEqual(a.frame().left, angry_first.left));
        a.SetEmotion(EyeEmotion::Neutral);
        a.SetEmotion(EyeEmotion::Angry);
        assert(GeometryEqual(a.frame().left, angry_first.left));

        EmotionController b;
        b.SetEmotion(EyeEmotion::Angry);
        assert(GeometryEqual(a.frame().left, b.frame().left));
        assert(GeometryEqual(a.frame().right, b.frame().right));
    }

    return 0;
}
