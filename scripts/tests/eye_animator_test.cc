#include "eye/eye_animator.h"
#include "eye/eye_frame.h"
#include "eye/eye_intent.h"
#include "eye/eye_intent_mailbox.h"

#include <cassert>

namespace {

bool GeometryEqual(const EyeGeometry& a, const EyeGeometry& b) {
    return a.center_x == b.center_x && a.center_y == b.center_y && a.width == b.width &&
           a.height == b.height && a.corner_radius == b.corner_radius &&
           a.rotation_degrees == b.rotation_degrees;
}

bool FramesEqual(const EyeFrame& a, const EyeFrame& b) {
    return GeometryEqual(a.left, b.left) && GeometryEqual(a.right, b.right) &&
           a.opacity == b.opacity;
}

}  // namespace

int main() {
    // Slice 2: EyeAnimator is pass-through. Update() must never change the
    // frame set by Reset() — there are no controller/target inputs yet.
    EyeFrame seed{};
    seed.left = {10.0f, 20.0f, 30.0f, 40.0f, 5.0f, 0.0f};
    seed.right = {50.0f, 20.0f, 30.0f, 40.0f, 5.0f, 0.0f};
    seed.opacity = 0.75f;

    EyeAnimator animator;
    animator.Reset(seed);
    assert(FramesEqual(animator.frame(), seed));
    animator.Update(16);
    assert(FramesEqual(animator.frame(), seed));
    animator.Update(1000);
    assert(FramesEqual(animator.frame(), seed));

    // Shared lerp helpers must match MhaiBotFaceV2's legacy
    // ClampProgress/LerpInt behavior exactly (extracted, not restyled).
    assert(EyeAnimator::ClampProgress(0, 300) == 0);
    assert(EyeAnimator::ClampProgress(150, 300) == 500);
    assert(EyeAnimator::ClampProgress(300, 300) == 1000);
    assert(EyeAnimator::ClampProgress(500, 300) == 1000);
    assert(EyeAnimator::ClampProgress(100, 0) == 1000);

    assert(EyeAnimator::LerpInt(0, 100, 0) == 0);
    assert(EyeAnimator::LerpInt(0, 100, 500) == 50);
    assert(EyeAnimator::LerpInt(0, 100, 1000) == 100);
    assert(EyeAnimator::LerpInt(20, 10, 500) == 15);

    // EyeIntentMailbox: latest-wins, no queue, empty before first publish.
    EyeIntentMailbox mailbox;
    EyeIntent out{};
    assert(!mailbox.ConsumeLatest(&out));

    EyeIntent first{};
    first.activity = EyeActivity::Listening;
    first.emotion = EyeEmotion::Focused;
    first.look_x = 0.5f;
    first.look_y = -0.25f;
    first.blink_allowed = false;
    mailbox.Publish(first);

    EyeIntent second{};
    second.activity = EyeActivity::Sleeping;
    second.emotion = EyeEmotion::Sleepy;
    mailbox.Publish(second);

    assert(mailbox.ConsumeLatest(&out));
    assert(out.activity == EyeActivity::Sleeping);
    assert(out.emotion == EyeEmotion::Sleepy);
    assert(out.blink_allowed == true);

    // ConsumeLatest again returns the same latest snapshot (no queue drain).
    EyeIntent out2{};
    assert(mailbox.ConsumeLatest(&out2));
    assert(out2.activity == EyeActivity::Sleeping);

    return 0;
}
