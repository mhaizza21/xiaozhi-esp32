#include "eye/eye_animation_coordinator.h"
#include "eye/eye_frame.h"
#include "eye/eye_intent.h"

#include <cassert>
#include <cmath>
#include <initializer_list>

namespace {

EyeFrame MakeBaseFrame() {
    // Representative Focused-equivalent base (matches EmotionController's
    // canonical Focused numbers under the default Config), used only to
    // exercise Compose() deltas — not imported from EmotionController to
    // keep this test independent of its internal table.
    EyeFrame frame{};
    frame.left = {77.0f, 84.0f, 52.0f, 48.0f, 16.0f, 0.0f};
    frame.right = {163.0f, 84.0f, 52.0f, 48.0f, 16.0f, 0.0f};
    frame.opacity = 1.0f;
    return frame;
}

EyeIntent MakeIntent(EyeActivity activity, bool blink_allowed = true) {
    EyeIntent intent{};
    intent.activity = activity;
    intent.emotion = EyeEmotion::Focused;
    intent.blink_allowed = blink_allowed;
    return intent;
}

bool GeometryEqual(const EyeGeometry& a, const EyeGeometry& b) {
    return a.center_x == b.center_x && a.center_y == b.center_y && a.width == b.width &&
           a.height == b.height && a.corner_radius == b.corner_radius &&
           a.rotation_degrees == b.rotation_degrees;
}

bool GeometryFinite(const EyeGeometry& g) {
    return std::isfinite(g.center_x) && std::isfinite(g.center_y) && std::isfinite(g.width) &&
           std::isfinite(g.height) && std::isfinite(g.corner_radius);
}

}  // namespace

int main() {
    const EyeFrame base = MakeBaseFrame();

    // 1. Neutral/Idle activity (and, separately, no-delta activities in
    // general) leave the base frame untouched — "Focused/Confident base
    // receives no activity delta."
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle));
        const EyeFrame composed = coordinator.Compose(base);
        assert(GeometryEqual(composed.left, base.left));
        assert(GeometryEqual(composed.right, base.right));

        coordinator.SetIntent(MakeIntent(EyeActivity::Speaking));
        const EyeFrame speaking_composed = coordinator.Compose(base);
        assert(GeometryEqual(speaking_composed.left, base.left));  // no delta for Speaking either
    }

    // 2. Thinking: exact reviewed deltas.
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Thinking));
        const EyeFrame composed = coordinator.Compose(base);
        assert(composed.left.center_x == base.left.center_x - 12.0f);
        assert(composed.right.center_x == base.right.center_x - 4.0f);
        assert(composed.left.center_y == base.left.center_y);
        assert(composed.right.center_y == base.right.center_y);
        assert(composed.left.width == base.left.width);
        assert(composed.right.width == base.right.width);
        assert(composed.left.height == base.left.height - 2.0f);
        assert(composed.right.height == base.right.height - 2.0f);
        assert(composed.left.corner_radius == base.left.corner_radius);
    }

    // 3. Listening: exact reviewed deltas.
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Listening));
        const EyeFrame composed = coordinator.Compose(base);
        assert(composed.left.center_x == base.left.center_x - 4.0f);
        assert(composed.right.center_x == base.right.center_x + 14.0f);
        assert(composed.left.center_y == base.left.center_y + 5.0f);
        assert(composed.right.center_y == base.right.center_y + 5.0f);
        assert(composed.left.width == base.left.width + 10.0f);
        assert(composed.right.width == base.right.width + 10.0f);
        assert(composed.left.height == base.left.height + 22.0f);
        assert(composed.right.height == base.right.height + 22.0f);
    }

    // 4. Activity priority behavior (05 §7 ordering).
    {
        EyeAnimationCoordinator coordinator;

        coordinator.SetIntent(MakeIntent(EyeActivity::Error));
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kError);

        coordinator.SetIntent(MakeIntent(EyeActivity::Sleeping));
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kSleepWake);
        coordinator.SetIntent(MakeIntent(EyeActivity::Waking));
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kSleepWake);

        coordinator.SetIntent(MakeIntent(EyeActivity::Idle));
        coordinator.SetTransient(EyeTransient::TouchReaction);
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kInteraction);
        coordinator.SetTransient(EyeTransient::None);

        coordinator.SetIntent(MakeIntent(EyeActivity::Listening));
        assert(coordinator.CurrentPriority() ==
               EyeAnimationCoordinator::Priority::kSpeakingListeningThinking);
        coordinator.SetIntent(MakeIntent(EyeActivity::Thinking));
        assert(coordinator.CurrentPriority() ==
               EyeAnimationCoordinator::Priority::kSpeakingListeningThinking);
        coordinator.SetIntent(MakeIntent(EyeActivity::Speaking));
        assert(coordinator.CurrentPriority() ==
               EyeAnimationCoordinator::Priority::kSpeakingListeningThinking);

        coordinator.SetIntent(MakeIntent(EyeActivity::Idle));
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kIdle);

        // An active transient outranks Speaking/Listening/Thinking too —
        // "Direct user interaction" sits above them in 05 §7.
        coordinator.SetIntent(MakeIntent(EyeActivity::Listening));
        coordinator.SetTransient(EyeTransient::Glance);
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kInteraction);
        coordinator.SetTransient(EyeTransient::None);

        // Sleep/Wake and Error outrank an active transient.
        coordinator.SetIntent(MakeIntent(EyeActivity::Sleeping));
        coordinator.SetTransient(EyeTransient::Blink);
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kSleepWake);
        coordinator.SetIntent(MakeIntent(EyeActivity::Error));
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kError);
    }

    // 5. Idle-enable policy: permitted only for exact Idle activity with no
    // active transient.
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle));
        assert(coordinator.IdleAllowed());

        coordinator.SetTransient(EyeTransient::Blink);
        assert(!coordinator.IdleAllowed());
        coordinator.SetTransient(EyeTransient::None);
        assert(coordinator.IdleAllowed());

        coordinator.SetIntent(MakeIntent(EyeActivity::Booting));
        assert(!coordinator.IdleAllowed());
        coordinator.SetIntent(MakeIntent(EyeActivity::Listening));
        assert(!coordinator.IdleAllowed());
        coordinator.SetIntent(MakeIntent(EyeActivity::Thinking));
        assert(!coordinator.IdleAllowed());
        coordinator.SetIntent(MakeIntent(EyeActivity::Speaking));
        assert(!coordinator.IdleAllowed());
    }

    // 6. Blink-allowed policy: mirrors EyeIntent.blink_allowed exactly
    // (already correctly computed per-activity by EyeActivityAdapter).
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, /*blink_allowed=*/true));
        assert(coordinator.BlinkAllowed());
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, /*blink_allowed=*/false));
        assert(!coordinator.BlinkAllowed());
    }

    // 7. Error / Sleeping / Waking suppression: both idle and blink denied
    // when the adapter has already set blink_allowed=false for these
    // activities (matching ADR-002's documented defaults).
    {
        EyeAnimationCoordinator coordinator;
        for (EyeActivity activity : {EyeActivity::Error, EyeActivity::Sleeping,
                                      EyeActivity::Waking, EyeActivity::Booting}) {
            coordinator.SetIntent(MakeIntent(activity, /*blink_allowed=*/false));
            assert(!coordinator.IdleAllowed());
            assert(!coordinator.BlinkAllowed());
        }
    }

    // 8. Deterministic output: identical intent + base always composes
    // identically; Update(dt) does not perturb it (no coordinator-owned
    // time-based state yet).
    {
        EyeAnimationCoordinator a;
        EyeAnimationCoordinator b;
        a.SetIntent(MakeIntent(EyeActivity::Listening));
        b.SetIntent(MakeIntent(EyeActivity::Listening));
        a.Update(16);
        b.Update(4294967295u);  // extreme dt must not matter
        const EyeFrame fa = a.Compose(base);
        const EyeFrame fb = b.Compose(base);
        assert(GeometryEqual(fa.left, fb.left));
        assert(GeometryEqual(fa.right, fb.right));

        const EyeFrame fa_again = a.Compose(base);
        assert(GeometryEqual(fa.left, fa_again.left));
    }

    // 9. Geometry safety: every activity's composed output stays finite
    // and positive-sized (07 §11), given a finite positive-sized base.
    {
        EyeAnimationCoordinator coordinator;
        const EyeActivity all[] = {EyeActivity::Booting,   EyeActivity::Idle,
                                    EyeActivity::Listening, EyeActivity::Thinking,
                                    EyeActivity::Speaking,  EyeActivity::Sleeping,
                                    EyeActivity::Waking,    EyeActivity::Error};
        for (EyeActivity activity : all) {
            coordinator.SetIntent(MakeIntent(activity));
            const EyeFrame composed = coordinator.Compose(base);
            assert(GeometryFinite(composed.left) && GeometryFinite(composed.right));
            assert(composed.left.width > 0.0f && composed.left.height > 0.0f);
            assert(composed.right.width > 0.0f && composed.right.height > 0.0f);
            assert(composed.left.center_x < composed.right.center_x);
        }
    }

    return 0;
}
