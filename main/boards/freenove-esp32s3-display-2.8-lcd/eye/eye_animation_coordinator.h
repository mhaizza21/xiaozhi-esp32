#ifndef MHAIBOT_EYE_ANIMATION_COORDINATOR_H
#define MHAIBOT_EYE_ANIMATION_COORDINATOR_H

#include "eye_frame.h"
#include "eye_intent.h"

#include <cstdint>

// LVGL-free coordinator (02 / 09 Slices 7-8). Owns priority resolution
// (05 §7), activity-specific geometry adjustment (07 §9 "Activity
// adjustment"), the steady-state emotion transition (inventory T5), and
// pet/startle/groggy transient composition (inventory T1/T3/T4) — nothing
// else. Consumes EmotionController::BasePose (a public static table) for
// the emotion geometry it needs; does not own an EmotionController
// instance (see Compose()).
//
// Shadow-only through Slice 8: Compose() output is never fed to
// LVGLEyeRenderer. Legacy ResolveRenderedPose/ResolveBasePose/
// InterpolatePose/PettingPose/StartledPose/GroggyPose remain sole pixel
// authority until Slice 11.
//
// Private transient identity (Slice 8): the canonical EyeTransient enum
// (Blink/DoubleBlink/Glance/TouchReaction, 05 §3.3) has no distinct values
// for Petting/Startled/GroggyWake — Petting/Startled are represented on
// the canonical signal as EyeTransient::TouchReaction (matching 05 §7's
// "Direct user interaction" priority tier), while GroggyWake corresponds
// to EyeActivity::Waking in the canonical model, not a transient, and does
// not touch the canonical transient() signal at all. The richer identity
// needed for geometry/timing (which specific transient, and its own
// elapsed time) lives in the private TransientKind enum below — it is
// deliberately a different type from EyeTransient and is never assigned
// to or derived from it, so it cannot leak into the canonical public
// model (EyeIntent/EyeTransient).
class EyeAnimationCoordinator {
public:
    // Priority tiers (05 §7), highest first. "Emotion transition" from 05
    // §7 sits between kSpeakingListeningThinking and kIdle but has no
    // representable ranking signal (it would require knowing "is idle
    // preempted by an in-progress transition", which 05 §7 lists but which
    // this coordinator does not attempt to rank distinctly — a documented
    // limitation carried from Slice 7, not a contradiction: Slice 8 gives
    // transitions real geometry timing but does not add a new priority
    // tier for them).
    enum class Priority {
        kIdle = 0,
        kSpeakingListeningThinking = 1,
        kInteraction = 2,  // 05 §7 "Direct user interaction" — an active EyeTransient
        kSleepWake = 3,
        kError = 4,
    };

    // Private transient identity (see class comment). Exposed read-only
    // via transient_kind() for testing/introspection only — not part of
    // the canonical EyeIntent/EyeTransient model.
    enum class TransientKind { kNone, kPetting, kStartled, kGroggyWake };

    EyeAnimationCoordinator();

    // Latest resolved intent (already computed by EyeActivityAdapter —
    // this does not re-derive DeviceState legality or activity overrides).
    // Slice 8: edge-detects an emotion change and begins a shadow steady-
    // state transition (inventory T5), mirroring
    // MhaiBotFaceV2::SetEmotion's CancelTransientAnimation() +
    // BeginTransitionTo(emotion) pairing — including canceling any active
    // shadow transient first, exactly as legacy does.
    void SetIntent(const EyeIntent& intent);

    // Canonical transient signal (05 §3.3). Kept for callers that only
    // need to know "is some interaction happening" (e.g. CurrentPriority).
    // StartPetting()/StartStartled() below also drive this to
    // TouchReaction; StartGroggyWake() does not (see class comment).
    void SetTransient(EyeTransient transient);

    // Slice 8 shadow entry points mirroring MhaiBotFaceV2::Start*/Cancel*
    // signatures (09 Slice 7/8: "keep signatures"). These do not affect
    // pixels — MhaiBotFaceV2's own Start*/Cancel* continue to drive the
    // legacy transient_mode_ path unchanged; a caller may additionally
    // call these for shadow parity (dual-publish), matching the pattern
    // already established for PublishIntent since Slice 3.
    void StartPetting();
    void StartStartled();
    void StartGroggyWake();
    void CancelTransient();

    // Accumulates transient/transition elapsed time and auto-exits a
    // transient whose duration has elapsed, mirroring
    // MhaiBotFaceV2::Tick's post-render exit checks. O(1), no heap
    // allocation, deterministic (no randomness).
    void Update(uint32_t delta_ms);

    Priority CurrentPriority() const;

    // Full shadow composition (07 §9 order applied so far: base emotion ->
    // activity adjustment -> transition/transient). Pure/const, recomputed
    // from current state each call (cheap arithmetic, no caching needed).
    // If a shadow transient is active, returns its dedicated geometry,
    // bypassing activity delta entirely — mirroring
    // MhaiBotFaceV2::ResolveRenderedPose's mutually exclusive if/else-if
    // priority (transients override, they do not layer on top of activity
    // adjustment). Otherwise interpolates the ongoing steady-state
    // transition, which naturally converges to the activity-adjusted
    // target once elapsed >= the transition duration — subsuming Slice 7's
    // old steady-state behavior as the converged case of the same lerp.
    // SHADOW ONLY — never fed to Render.
    EyeFrame Compose() const;

    // Policy queries derived from canonical EyeIntent plus the private
    // transient identity (05 §5/§7, 07 §5.3/§7). NOT wired to replace
    // MhaiBotFaceV2's existing local-state-anchored blink/idle checks in
    // Slice 7/8 — see mhaibot_face_v2.cc comments and the Slice 7/8
    // reports for why (a narrow, already-audited cross-timer desync
    // between the mailbox and face-local transient state during
    // Sleeping->Waking that these queries do not resolve).
    bool IdleAllowed() const;
    bool BlinkAllowed() const;

    const EyeIntent& intent() const { return intent_; }
    EyeTransient transient() const { return transient_; }
    TransientKind transient_kind() const { return transient_kind_; }

private:
    // Duration/timing constants matching MhaiBotFaceV2::Config's default
    // transition_ms and the mhaibot_interaction_model.h duration functions
    // exactly (MhaiBotPetDurationMs/MhaiBotStartleDurationMs/
    // MhaiBotGroggyWakeDurationMs) — duplicated here, not included from
    // that board-specific header, to keep eye/ independent of the
    // interaction-model file (same duplication pattern already used for
    // MhaiBotFaceV2::Config's geometry defaults in EmotionController,
    // Slice 6).
    static constexpr uint32_t kTransitionMs = 300;
    static constexpr uint32_t kPetDurationMs = 3000;
    static constexpr uint32_t kStartleDurationMs = 700;
    static constexpr uint32_t kGroggyDurationMs = 5000;
    // Nominal tick period used only to convert Pet's legacy frame-count-
    // based sway/bob/opacity shimmer into a time-based equivalent (see
    // ComposePetting in the .cc and the Slice 8 report). Matches
    // MhaiBotFaceV2::Config's default tick_ms. This is a documented
    // approximation, not a source of new per-frame randomness.
    static constexpr uint32_t kNominalTickMs = 33;

    void BeginTransition(const EyeFrame& from, const EyeFrame& to);
    EyeFrame ApplyActivityDelta(const EyeFrame& base) const;
    EyeFrame ComposeSteadyState() const;
    EyeFrame ComposePetting() const;
    EyeFrame ComposeStartled() const;
    EyeFrame ComposeGroggy() const;
    static EyeFrame LerpFrame(const EyeFrame& from, const EyeFrame& to, uint16_t progress_per_mille);

    EyeIntent intent_{};
    EyeTransient transient_ = EyeTransient::None;

    TransientKind transient_kind_ = TransientKind::kNone;
    uint32_t transient_elapsed_ms_ = 0;
    EyeFrame pet_anchor_{};  // frame captured at the instant StartPetting() was called

    EyeFrame transition_from_{};
    EyeFrame transition_to_{};
    uint32_t transition_elapsed_ms_ = kTransitionMs;  // starts converged
};

#endif  // MHAIBOT_EYE_ANIMATION_COORDINATOR_H
