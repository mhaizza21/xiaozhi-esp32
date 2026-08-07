#include "eye/emotion_controller.h"
#include "eye/eye_animation_coordinator.h"
#include "eye/eye_frame.h"
#include "eye/eye_intent.h"

#include <cassert>
#include <cmath>
#include <initializer_list>

namespace {

EyeIntent MakeIntent(EyeActivity activity, EyeEmotion emotion = EyeEmotion::Focused,
                      bool blink_allowed = true) {
    EyeIntent intent{};
    intent.activity = activity;
    intent.emotion = emotion;
    intent.blink_allowed = blink_allowed;
    return intent;
}

bool GeometryEqual(const EyeGeometry& a, const EyeGeometry& b) {
    return a.center_x == b.center_x && a.center_y == b.center_y && a.width == b.width &&
           a.height == b.height && a.corner_radius == b.corner_radius &&
           a.rotation_degrees == b.rotation_degrees;
}

bool NearlyEqual(float a, float b, float tolerance) {
    return std::fabs(a - b) <= tolerance;
}

bool GeometryFinite(const EyeGeometry& g) {
    return std::isfinite(g.center_x) && std::isfinite(g.center_y) && std::isfinite(g.width) &&
           std::isfinite(g.height) && std::isfinite(g.corner_radius);
}

}  // namespace

int main() {
    const EyeFrame focused = EmotionController::BasePose(EyeEmotion::Focused);
    const EyeFrame neutral = EmotionController::BasePose(EyeEmotion::Neutral);
    const EyeFrame happy = EmotionController::BasePose(EyeEmotion::Happy);

    // ================================================================
    // 1. Activity adjustment (Slice 7, re-verified against the new
    // parameterless Compose(); a SetIntent-triggered transition to
    // Focused is forced to converge via Update(300) before asserting,
    // since Slice 8 makes emotion changes real 300ms transitions).
    // ================================================================
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle));
        coordinator.Update(300);
        const EyeFrame composed = coordinator.Compose();
        assert(GeometryEqual(composed.left, focused.left));
        assert(GeometryEqual(composed.right, focused.right));

        coordinator.SetIntent(MakeIntent(EyeActivity::Speaking));  // same emotion, no re-transition
        const EyeFrame speaking_composed = coordinator.Compose();
        assert(GeometryEqual(speaking_composed.left, focused.left));  // no delta for Speaking either
    }

    // 2. Thinking: exact reviewed deltas (unchanged from Slice 7).
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Thinking));
        coordinator.Update(300);
        const EyeFrame composed = coordinator.Compose();
        assert(composed.left.center_x == focused.left.center_x - 12.0f);
        assert(composed.right.center_x == focused.right.center_x - 4.0f);
        assert(composed.left.center_y == focused.left.center_y);
        assert(composed.left.height == focused.left.height - 2.0f);
        assert(composed.left.corner_radius == focused.left.corner_radius);
    }

    // 3. Listening: exact reviewed deltas (unchanged from Slice 7).
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Listening));
        coordinator.Update(300);
        const EyeFrame composed = coordinator.Compose();
        assert(composed.left.center_x == focused.left.center_x - 4.0f);
        assert(composed.right.center_x == focused.right.center_x + 14.0f);
        assert(composed.left.center_y == focused.left.center_y + 5.0f);
        assert(composed.left.width == focused.left.width + 10.0f);
        assert(composed.left.height == focused.left.height + 22.0f);
    }

    // 4. Activity priority behavior (05 §7 ordering) — unchanged from
    // Slice 7.
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

        coordinator.SetIntent(MakeIntent(EyeActivity::Idle));
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kIdle);

        coordinator.SetIntent(MakeIntent(EyeActivity::Sleeping));
        coordinator.SetTransient(EyeTransient::Blink);
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kSleepWake);
        coordinator.SetIntent(MakeIntent(EyeActivity::Error));
        assert(coordinator.CurrentPriority() == EyeAnimationCoordinator::Priority::kError);
    }

    // 5. Idle-enable policy: permitted only for exact Idle activity with no
    // active canonical transient AND no active shadow transient (Slice 8
    // extends this with transient_kind_).
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

        coordinator.SetIntent(MakeIntent(EyeActivity::Idle));
        assert(coordinator.IdleAllowed());
        coordinator.StartPetting();
        assert(!coordinator.IdleAllowed());  // shadow transient also suppresses idle
        coordinator.CancelTransient();
        assert(coordinator.IdleAllowed());
    }

    // 6. Blink-allowed policy: mirrors EyeIntent.blink_allowed, plus
    // Slice 8's GroggyWake-only shadow suppression (matching legacy's
    // exact scope — Petting/Startled remain unsuppressed, matching
    // legacy, which does NOT suppress blink for those either).
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, EyeEmotion::Focused, true));
        assert(coordinator.BlinkAllowed());
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, EyeEmotion::Focused, false));
        assert(!coordinator.BlinkAllowed());

        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, EyeEmotion::Focused, true));
        assert(coordinator.BlinkAllowed());
        coordinator.StartPetting();
        assert(coordinator.BlinkAllowed());  // NOT suppressed for Petting (matches legacy)
        coordinator.CancelTransient();
        coordinator.StartStartled();
        assert(coordinator.BlinkAllowed());  // NOT suppressed for Startled (matches legacy)
        coordinator.CancelTransient();
        coordinator.StartGroggyWake();
        assert(!coordinator.BlinkAllowed());  // suppressed for GroggyWake
    }

    // 7. Error / Sleeping / Waking suppression (unchanged from Slice 7).
    {
        EyeAnimationCoordinator coordinator;
        for (EyeActivity activity : {EyeActivity::Error, EyeActivity::Sleeping,
                                      EyeActivity::Waking, EyeActivity::Booting}) {
            coordinator.SetIntent(MakeIntent(activity, EyeEmotion::Focused, false));
            assert(!coordinator.IdleAllowed());
            assert(!coordinator.BlinkAllowed());
        }
    }

    // 8. Deterministic output for steady-state composition.
    {
        EyeAnimationCoordinator a;
        EyeAnimationCoordinator b;
        a.SetIntent(MakeIntent(EyeActivity::Listening));
        b.SetIntent(MakeIntent(EyeActivity::Listening));
        a.Update(300);
        b.Update(4294967295u);  // extreme dt must not overflow/misbehave
        const EyeFrame fa = a.Compose();
        const EyeFrame fb = b.Compose();
        assert(GeometryEqual(fa.left, fb.left));
        assert(GeometryEqual(fa.right, fb.right));

        const EyeFrame fa_again = a.Compose();
        assert(GeometryEqual(fa.left, fa_again.left));
    }

    // 9. Geometry safety: every activity's steady-state composed output
    // stays finite and positive-sized (07 §11).
    {
        EyeAnimationCoordinator coordinator;
        const EyeActivity all[] = {EyeActivity::Booting,   EyeActivity::Idle,
                                    EyeActivity::Listening, EyeActivity::Thinking,
                                    EyeActivity::Speaking,  EyeActivity::Sleeping,
                                    EyeActivity::Waking,    EyeActivity::Error};
        for (EyeActivity activity : all) {
            coordinator.SetIntent(MakeIntent(activity));
            coordinator.Update(300);
            const EyeFrame composed = coordinator.Compose();
            assert(GeometryFinite(composed.left) && GeometryFinite(composed.right));
            assert(composed.left.width > 0.0f && composed.left.height > 0.0f);
            assert(composed.right.width > 0.0f && composed.right.height > 0.0f);
            assert(composed.left.center_x < composed.right.center_x);
        }
    }

    // ================================================================
    // 10. Steady-state emotion transition (T5): t=0, midpoint, boundary,
    // zero/extreme dt.
    // ================================================================
    {
        EyeAnimationCoordinator coordinator;  // starts converged at Neutral
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, EyeEmotion::Happy));

        // t=0: immediately after the transition begins, before any
        // Update(), the composed frame equals the "from" (Neutral) exactly.
        EyeFrame composed = coordinator.Compose();
        assert(GeometryEqual(composed.left, neutral.left));

        // zero dt: no change.
        coordinator.Update(0);
        composed = coordinator.Compose();
        assert(GeometryEqual(composed.left, neutral.left));

        // midpoint (150ms of 300ms): exact 50% lerp between Neutral and
        // Happy.
        coordinator.Update(150);
        composed = coordinator.Compose();
        const float expected_mid_y = neutral.left.center_y + (happy.left.center_y - neutral.left.center_y) * 0.5f;
        const float expected_mid_h = neutral.left.height + (happy.left.height - neutral.left.height) * 0.5f;
        assert(NearlyEqual(composed.left.center_y, expected_mid_y, 0.01f));
        assert(NearlyEqual(composed.left.height, expected_mid_h, 0.01f));

        // boundary (300ms total): exact Happy.
        coordinator.Update(150);
        composed = coordinator.Compose();
        assert(GeometryEqual(composed.left, happy.left));

        // extreme dt beyond the boundary: stays exactly at Happy (already
        // converged; ClampProgress saturates).
        coordinator.Update(4294967295u);
        composed = coordinator.Compose();
        assert(GeometryEqual(composed.left, happy.left));
    }

    // ================================================================
    // 11. Pet parity: t=0, t=300ms (lerp-in boundary), mid-hold (1500ms),
    // t=3000ms (last rendered pet frame), then auto-exit.
    // ================================================================
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, EyeEmotion::Focused));
        coordinator.Update(300);  // converge to steady Focused first
        const EyeFrame anchor = coordinator.Compose();

        coordinator.StartPetting();
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kPetting);
        assert(coordinator.transient() == EyeTransient::TouchReaction);

        // t=0: exact anchor (pre-pet frame), sway/bob/opacity at
        // pseudo_frame=0 (sway=-2, bob=-1, glow_phase=0 -> opa=200).
        EyeFrame composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.center_x, anchor.left.center_x - 2.0f, 0.001f));
        assert(NearlyEqual(composed.left.center_y, anchor.left.center_y - 1.0f, 0.001f));
        assert(NearlyEqual(composed.opacity, 200.0f / 255.0f, 0.001f));

        // t=300ms (exact lerp-in boundary): geometry (pre sway/bob)
        // reaches PettingBase() exactly. pseudo_frame = 300/33 = 9 ->
        // sway=(9/5)%5-2=1-2=-1, bob=(9/8)%3-1=1-1=0.
        coordinator.Update(300);
        composed = coordinator.Compose();
        // PettingBase: Happy with height=10, radius=5, y+=12 ->
        // center_y = (66+12) + 10/2 = 78+5 = 83; center_x unchanged (73/167).
        assert(NearlyEqual(composed.left.center_x, 73.0f - 1.0f, 0.001f));
        assert(NearlyEqual(composed.left.center_y, 83.0f + 0.0f, 0.001f));
        assert(NearlyEqual(composed.left.height, 10.0f, 0.001f));
        assert(NearlyEqual(composed.left.corner_radius, 5.0f, 0.001f));

        // Mid-hold (1500ms): geometry stays at PettingBase() (only
        // sway/bob/opacity vary) — pseudo_frame = 1500/33 = 45 ->
        // sway=(45/5)%5-2=(9%5)-2=4-2=2, bob=(45/8)%3-1=(5%3)-1=2-1=1.
        coordinator.Update(1200);  // total elapsed 1500ms
        composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.height, 10.0f, 0.001f));
        assert(NearlyEqual(composed.left.center_x, 73.0f + 2.0f, 0.001f));
        assert(NearlyEqual(composed.left.center_y, 83.0f + 1.0f, 0.001f));

        // t=3000ms (exact duration boundary): last rendered frame before
        // exit still shows pet geometry (Update()'s exit check runs AFTER
        // the caller would have rendered this Compose() result — the
        // exit itself is only applied on the NEXT Update() call in this
        // API, matching MhaiBotFaceV2::Tick's "exit check after ApplyPose"
        // ordering).
        coordinator.Update(1500);  // total elapsed 3000ms; auto-exit fires now
        // transient has already auto-exited inside this Update() call.
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kNone);
        assert(coordinator.transient() == EyeTransient::None);
        // The exit transition begins from the pet's own last frame
        // (captured internally before clearing), so Compose() immediately
        // after exit is continuous with the pet geometry, not a jump.
        composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.height, 10.0f, 0.001f));  // still pet-height at the instant of exit

        // Fully converges back to steady Focused after another 300ms.
        coordinator.Update(300);
        composed = coordinator.Compose();
        assert(GeometryEqual(composed.left, focused.left));
    }

    // ================================================================
    // 12. Startle parity: t=0 (exact StartledPose), t=350ms (exact 50%),
    // t=700ms (exact target base).
    // ================================================================
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, EyeEmotion::Focused));
        coordinator.Update(300);
        coordinator.StartStartled();
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kStartled);

        // t=0: StartledBase(Focused) exactly. Hand-derived from Focused's
        // origin-space pose (left_x=51,right_x=137,y=60,width=52,
        // height=48,radius=16): left_x-7=44,right_x+7=144,width+14=66,
        // height+20=68,y-8=52 -> center: left=44+33=77, right=144+33=177,
        // center_y=52+34=86, width=66, height=68.
        EyeFrame composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.center_x, 77.0f, 0.001f));
        assert(NearlyEqual(composed.right.center_x, 177.0f, 0.001f));
        assert(NearlyEqual(composed.left.center_y, 86.0f, 0.001f));
        assert(NearlyEqual(composed.left.width, 66.0f, 0.001f));
        assert(NearlyEqual(composed.left.height, 68.0f, 0.001f));

        // t=350ms (exact 50%): midpoint between StartledBase and Focused.
        coordinator.Update(350);
        composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.center_x, (77.0f + focused.left.center_x) * 0.5f, 0.01f));
        assert(NearlyEqual(composed.right.center_x, (177.0f + focused.right.center_x) * 0.5f, 0.01f));
        assert(NearlyEqual(composed.left.width, (66.0f + focused.left.width) * 0.5f, 0.01f));
        assert(NearlyEqual(composed.left.height, (68.0f + focused.left.height) * 0.5f, 0.01f));

        // t=700ms (exact boundary): exact Focused base (no activity
        // delta layered — transients bypass it, matching legacy).
        coordinator.Update(350);
        composed = coordinator.Compose();
        assert(GeometryEqual(composed.left, focused.left));
        assert(GeometryEqual(composed.right, focused.right));

        // Auto-exit at the boundary tick.
        coordinator.Update(1);
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kNone);
    }

    // ================================================================
    // 13. Groggy parity: t=0, t=745/750ms (blink-clamp seam continuity),
    // t=1500ms (300‰ segment boundary), t=1505ms (first 301‰ point,
    // integer-truncation edge), t=5000ms (final clamp quirk: height=40,
    // not Neutral's true 62).
    // ================================================================
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, EyeEmotion::Sleepy));
        coordinator.StartGroggyWake();
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kGroggyWake);
        assert(coordinator.transient() == EyeTransient::None);  // no canonical leakage (see §14)

        // t=0: ResolveBasePose(kSleeping) exactly — height=8, unclamped
        // (blink ceiling=100 at progress=0). center_y = 80 + 4 = 84.
        EyeFrame composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.height, 8.0f, 0.001f));
        assert(NearlyEqual(composed.left.center_y, 84.0f, 0.001f));
        assert(NearlyEqual(composed.left.center_x, 73.0f, 0.001f));

        // t=745ms / t=750ms: blink-clamp-ceiling seam continuity
        // (progress 149 -> 150, formula branch switch at strict <150).
        coordinator.Update(745);
        composed = coordinator.Compose();
        const float height_at_745 = composed.left.height;
        assert(height_at_745 < 40.0f);  // unclamped at this point
        coordinator.Update(5);  // total 750ms
        composed = coordinator.Compose();
        const float height_at_750 = composed.left.height;
        assert(height_at_750 < 40.0f);
        assert(NearlyEqual(height_at_745, height_at_750, 0.5f));  // no visible jump across the seam

        // t=1500ms (progress=300‰ exactly, segment-switch boundary,
        // inclusive on branch 1): ResolveBasePose(kSleepy) exactly,
        // unclamped (24 < 40).
        coordinator.Update(750);  // total 1500ms
        composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.height, 24.0f, 0.001f));
        assert(NearlyEqual(composed.left.center_y, 83.0f, 0.001f));

        // t=1501..1504ms: due to integer truncation
        // (elapsed*1000/5000), progress stays 300 (branch 1 / sleepy)
        // through this whole range — branch 2 does not truly begin until
        // elapsed_ms=1505.
        coordinator.Update(4);  // total 1504ms
        composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.height, 24.0f, 0.001f));  // still exactly sleepy

        // t=1505ms: first elapsed_ms where progress becomes 301 (branch 2
        // begins), remapped=(301-300)*1000/700=1 (truncated) -> a tiny
        // step away from Sleepy toward Neutral.
        coordinator.Update(1);  // total 1505ms
        composed = coordinator.Compose();
        assert(composed.left.height > 24.0f);
        assert(composed.left.height < 24.1f);

        // t=5000ms (exact duration boundary): pre-clamp geometry reaches
        // Neutral (height=62, y=58), but the pseudo-blink clamp still
        // limits height to 40 — the quirk the task requires preserving.
        coordinator.Update(3495);  // total 5000ms
        composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.height, 40.0f, 0.001f));
        assert(NearlyEqual(composed.left.center_y, 78.0f, 0.001f));  // y=58 unclamped, only height clamped

        // Auto-exit fires on this Update() call already (elapsed >= 5000).
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kNone);
        // Forced-Neutral exit transition begins from the clamped-40 frame
        // — full Neutral openness (height=62) is reached only after this
        // transition completes, not immediately.
        composed = coordinator.Compose();
        assert(NearlyEqual(composed.left.height, 40.0f, 0.001f));  // t=0 of the exit transition
        coordinator.Update(300);
        composed = coordinator.Compose();
        assert(GeometryEqual(composed.left, neutral.left));  // now fully Neutral
    }

    // ================================================================
    // 14. Cancellation/overwrite semantics + no leakage of private
    // transient identity into the canonical public model.
    // ================================================================
    {
        EyeAnimationCoordinator coordinator;
        coordinator.SetIntent(MakeIntent(EyeActivity::Idle, EyeEmotion::Focused));

        // Overwrite semantics: starting Startled while Petting is active
        // immediately switches (matches legacy's unconditional overwrite;
        // no arbitration).
        coordinator.StartPetting();
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kPetting);
        coordinator.StartStartled();
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kStartled);

        // CancelTransient clears shadow state and the canonical signal.
        coordinator.CancelTransient();
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kNone);
        assert(coordinator.transient() == EyeTransient::None);
        coordinator.CancelTransient();  // idempotent, matches legacy's guard

        // GroggyWake re-entry guard: calling StartGroggyWake() twice does
        // not reset elapsed time.
        coordinator.StartGroggyWake();
        coordinator.Update(1000);
        coordinator.StartGroggyWake();  // should be a no-op (already active)
        coordinator.Update(1);
        // (No direct elapsed-time accessor; verified indirectly via
        // geometry not resetting to t=0's height=8 after the second call.)
        const EyeFrame composed = coordinator.Compose();
        assert(composed.left.height != 8.0f);

        // No leakage: GroggyWake never touches the canonical transient()
        // signal (private TransientKind != public EyeTransient — see
        // class comment). Petting/Startled DO set the canonical signal to
        // TouchReaction, which is the intended, documented exception (05
        // §7 "Direct user interaction"), not a leak of the private enum
        // type itself — transient() always returns a valid EyeTransient,
        // never a TransientKind.
        coordinator.CancelTransient();
        coordinator.StartGroggyWake();
        assert(coordinator.transient() == EyeTransient::None);
        assert(coordinator.transient_kind() == EyeAnimationCoordinator::TransientKind::kGroggyWake);
    }

    return 0;
}
