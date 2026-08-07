#ifndef MHAIBOT_EYE_ANIMATION_COORDINATOR_H
#define MHAIBOT_EYE_ANIMATION_COORDINATOR_H

#include "eye_frame.h"
#include "eye_intent.h"

#include <cstdint>

// LVGL-free coordinator (02 / 09 Slice 7). Owns priority resolution (05 §7)
// and activity-specific geometry adjustments layered on top of an
// externally supplied emotion base frame (07 §9 "Activity adjustment"
// stage) — nothing else.
//
// Ownership note (deviation from 02's illustrative sketch, per the
// Slice 4-6 integration review Check 1): 02 shows the coordinator owning
// its own EmotionController/BlinkController/IdleController/EyeAnimator
// members. Those controllers already exist and are ticked directly by
// MhaiBotFaceV2 since Slices 4-6; a second, coordinator-owned set would be
// duplicated, divergent state, not a relocation. This coordinator therefore
// takes the emotion base frame as an explicit Compose() argument instead of
// owning an EmotionController, and does not instantiate Blink/Idle
// controllers at all — it only exposes read-only policy queries
// (IdleAllowed/BlinkAllowed) that a caller may consult.
//
// Shadow-only in Slice 7: Compose() output is never fed to
// LVGLEyeRenderer. Legacy ResolveRenderedPose remains sole pixel
// authority until Slice 11.
class EyeAnimationCoordinator {
public:
    // Priority tiers (05 §7), highest first. "Emotion transition" from 05
    // §7 sits between kSpeakingListeningThinking and kIdle but has no
    // representable signal yet (EmotionController::Update is a documented
    // no-op — there is no "transition in progress" state to rank); it is
    // therefore not a distinct enumerator here, and ranks as kIdle by
    // default. This is a documented limitation, not a contradiction: there
    // is nothing to rank differently until Slice 8 gives EmotionController
    // real transition timing (inventory T5).
    enum class Priority {
        kIdle = 0,
        kSpeakingListeningThinking = 1,
        kInteraction = 2,  // 05 §7 "Direct user interaction" — an active EyeTransient
        kSleepWake = 3,
        kError = 4,
    };

    // Latest resolved intent (already computed by EyeActivityAdapter —
    // this does not re-derive DeviceState legality or activity overrides).
    void SetIntent(const EyeIntent& intent);

    // Accepted per Slice 7 scope ("accept EyeTransient hooks"); stored
    // only, affects priority ranking. Real transient geometry composition
    // is Slice 8 — StartPetting/StartGroggyWake/StartStartled on
    // MhaiBotFaceV2 continue to forward to legacy transient_mode_ directly
    // and do not call this.
    void SetTransient(EyeTransient transient);

    // No-op today (see class comment on Update generally in sibling
    // controllers): there is no coordinator-owned time-based state yet.
    // Present to match the documented dt-based API and reserved for
    // future priority-transition timing (05 §7 "cancel or blend out the
    // idle glance" example, Slice 8+).
    void Update(uint32_t delta_ms);

    Priority CurrentPriority() const;

    // Activity-adjustment stage (07 §9): applies the exact reviewed
    // Thinking/Listening deltas on top of the given emotion base frame.
    // Focused/Confident (and every other activity) receives no delta.
    // Pure function of intent()/transient() and the argument — safe to
    // call at any time, does not mutate coordinator state.
    EyeFrame Compose(const EyeFrame& emotion_base_frame) const;

    // Policy queries derived from the canonical EyeIntent (05 §5/§7, 07
    // §5.3/§7) — NOT wired to replace MhaiBotFaceV2's existing local-state
    // checks in Slice 7 (see mhaibot_face_v2.cc comments and the Slice 7
    // report for why: both checks currently guard against a narrow
    // cross-timer desync between the mailbox and face-local transient
    // state during Sleeping->Waking that this coordinator cannot yet
    // resolve without risking new, less-audited behavior drift).
    bool IdleAllowed() const;
    bool BlinkAllowed() const;

    const EyeIntent& intent() const { return intent_; }
    EyeTransient transient() const { return transient_; }

private:
    EyeIntent intent_{};
    EyeTransient transient_ = EyeTransient::None;
};

#endif  // MHAIBOT_EYE_ANIMATION_COORDINATOR_H
