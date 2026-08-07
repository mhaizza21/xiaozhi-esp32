#include "eye_animation_coordinator.h"

#include "emotion_controller.h"
#include "eye_animator.h"

namespace {

// Private, Coordinator-internal representation — NOT the canonical
// EyeFrame/EyeGeometry model. Mirrors MhaiBotFaceV2::Pose exactly (shared
// y/width/height/radius, per-eye left_x/right_x) so legacy's transient
// formulas (PettingPose/StartledPose/GroggyPose) can be replicated
// faithfully in origin+size space, matching how legacy actually computes
// them, before the final center-space conversion (mirroring Slice 0's
// PoseToEyeFrame formula: center = origin + size/2). Building these
// directly in center-space risks hand-derivation errors, since some
// legacy offsets (e.g. StartledPose's per-eye left_x/right_x + shared
// width change) do not translate to a simple symmetric center delta.
struct TransientPose {
    float left_x = 0.0f;
    float right_x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float radius = 0.0f;
};

TransientPose FromEyeFrame(const EyeFrame& frame) {
    TransientPose pose;
    pose.width = frame.left.width;
    pose.height = frame.left.height;
    pose.radius = frame.left.corner_radius;
    pose.y = frame.left.center_y - pose.height * 0.5f;
    pose.left_x = frame.left.center_x - pose.width * 0.5f;
    pose.right_x = frame.right.center_x - pose.width * 0.5f;
    return pose;
}

EyeFrame ToEyeFrame(const TransientPose& pose, float opacity) {
    EyeFrame frame{};
    frame.left.center_x = pose.left_x + pose.width * 0.5f;
    frame.left.center_y = pose.y + pose.height * 0.5f;
    frame.left.width = pose.width;
    frame.left.height = pose.height;
    frame.left.corner_radius = pose.radius;
    frame.left.rotation_degrees = 0.0f;
    frame.right.center_x = pose.right_x + pose.width * 0.5f;
    frame.right.center_y = frame.left.center_y;
    frame.right.width = pose.width;
    frame.right.height = pose.height;
    frame.right.corner_radius = pose.radius;
    frame.right.rotation_degrees = 0.0f;
    frame.opacity = opacity;
    return frame;
}

TransientPose LerpTransientPose(const TransientPose& from, const TransientPose& to,
                                 uint16_t progress_per_mille) {
    TransientPose pose;
    pose.left_x = EyeAnimator::LerpFloat(from.left_x, to.left_x, progress_per_mille);
    pose.right_x = EyeAnimator::LerpFloat(from.right_x, to.right_x, progress_per_mille);
    pose.y = EyeAnimator::LerpFloat(from.y, to.y, progress_per_mille);
    pose.width = EyeAnimator::LerpFloat(from.width, to.width, progress_per_mille);
    pose.height = EyeAnimator::LerpFloat(from.height, to.height, progress_per_mille);
    pose.radius = EyeAnimator::LerpFloat(from.radius, to.radius, progress_per_mille);
    return pose;
}

TransientPose HappyBase() {
    return FromEyeFrame(EmotionController::BasePose(EyeEmotion::Happy));
}

TransientPose SleepyBase() {
    return FromEyeFrame(EmotionController::BasePose(EyeEmotion::Sleepy));
}

TransientPose NeutralBase() {
    return FromEyeFrame(EmotionController::BasePose(EyeEmotion::Neutral));
}

TransientPose SleepingBase() {
    // No canonical EyeEmotion for legacy's kSleeping (fully-closed sleep
    // pose) — Sleeping is an EyeActivity, not an EyeEmotion (05 §3), so
    // EmotionController deliberately has no entry for it (Slice 6).
    // Matches MhaiBotFaceV2::ResolveBasePose(kSleeping) exactly: eye_y+22,
    // height=8, same left/right/width/radius as Neutral.
    TransientPose pose = NeutralBase();
    pose.y += 22.0f;
    pose.height = 8.0f;
    return pose;
}

TransientPose PettingBase() {
    // Matches legacy PettingPose(): Happy base, height=10, radius=height/2,
    // y+=12.
    TransientPose pose = HappyBase();
    pose.height = 10.0f;
    pose.radius = pose.height * 0.5f;
    pose.y += 12.0f;
    return pose;
}

TransientPose StartledBase(const TransientPose& emotion_base) {
    // Matches legacy StartledPose(): left_x-=7, right_x+=7, width+=14,
    // height+=20, y-=8. Deliberately not symmetric in center-space (the
    // left eye's center is unchanged while the right eye's center shifts
    // +14px — an artifact of legacy's origin+size formula, preserved
    // exactly per "extract, don't restyle").
    TransientPose pose = emotion_base;
    pose.left_x -= 7.0f;
    pose.right_x += 7.0f;
    pose.width += 14.0f;
    pose.height += 20.0f;
    pose.y -= 8.0f;
    return pose;
}

}  // namespace

EyeAnimationCoordinator::EyeAnimationCoordinator() {
    const EyeFrame neutral = EmotionController::BasePose(EyeEmotion::Neutral);
    transition_from_ = neutral;
    transition_to_ = neutral;
}

void EyeAnimationCoordinator::SetIntent(const EyeIntent& intent) {
    const bool emotion_changed = (intent.emotion != intent_.emotion);
    EyeFrame from{};
    if (emotion_changed) {
        // Captures whatever was active under the OLD intent_ (steady
        // state or an active transient) — mirrors
        // MhaiBotFaceV2::BeginTransitionTo capturing current_pose_.
        from = Compose();
        if (transient_kind_ != TransientKind::kNone) {
            // Mirrors SetEmotion's CancelTransientAnimation() call before
            // BeginTransitionTo.
            transient_kind_ = TransientKind::kNone;
            transient_elapsed_ms_ = 0;
            SetTransient(EyeTransient::None);
        }
    }
    intent_ = intent;
    if (emotion_changed) {
        BeginTransition(from, ApplyActivityDelta(EmotionController::BasePose(intent_.emotion)));
    }
}

void EyeAnimationCoordinator::SetTransient(EyeTransient transient) {
    transient_ = transient;
}

void EyeAnimationCoordinator::StartPetting() {
    pet_anchor_ = Compose();
    transient_kind_ = TransientKind::kPetting;
    transient_elapsed_ms_ = 0;
    SetTransient(EyeTransient::TouchReaction);
}

void EyeAnimationCoordinator::StartStartled() {
    transient_kind_ = TransientKind::kStartled;
    transient_elapsed_ms_ = 0;
    SetTransient(EyeTransient::TouchReaction);
}

void EyeAnimationCoordinator::StartGroggyWake() {
    if (transient_kind_ == TransientKind::kGroggyWake) {
        return;
    }
    transient_kind_ = TransientKind::kGroggyWake;
    transient_elapsed_ms_ = 0;
    // No canonical SetTransient call — GroggyWake maps to
    // EyeActivity::Waking in the canonical model (see class comment).
}

void EyeAnimationCoordinator::CancelTransient() {
    if (transient_kind_ == TransientKind::kNone) {
        return;
    }
    transient_kind_ = TransientKind::kNone;
    transient_elapsed_ms_ = 0;
    SetTransient(EyeTransient::None);
}

void EyeAnimationCoordinator::Update(uint32_t delta_ms) {
    if (transition_elapsed_ms_ < kTransitionMs) {
        transition_elapsed_ms_ += delta_ms;
    }
    if (transient_kind_ != TransientKind::kNone) {
        transient_elapsed_ms_ += delta_ms;
    }

    // Auto-exit, mirroring MhaiBotFaceV2::Tick's post-render exit checks.
    // Each branch captures the transient's own last frame BEFORE clearing
    // state, then begins a fresh transition from it (matching legacy's
    // exit -> BeginTransitionTo pairing exactly, including GroggyWake's
    // forced-Neutral target).
    if (transient_kind_ == TransientKind::kPetting && transient_elapsed_ms_ >= kPetDurationMs) {
        const EyeFrame exit_frame = ComposePetting();
        transient_kind_ = TransientKind::kNone;
        transient_elapsed_ms_ = 0;
        SetTransient(EyeTransient::None);
        BeginTransition(exit_frame, ApplyActivityDelta(EmotionController::BasePose(intent_.emotion)));
    } else if (transient_kind_ == TransientKind::kGroggyWake &&
               transient_elapsed_ms_ >= kGroggyDurationMs) {
        const EyeFrame exit_frame = ComposeGroggy();
        transient_kind_ = TransientKind::kNone;
        transient_elapsed_ms_ = 0;
        BeginTransition(exit_frame,
                         ApplyActivityDelta(EmotionController::BasePose(EyeEmotion::Neutral)));
    } else if (transient_kind_ == TransientKind::kStartled &&
               transient_elapsed_ms_ >= kStartleDurationMs) {
        const EyeFrame exit_frame = ComposeStartled();
        transient_kind_ = TransientKind::kNone;
        transient_elapsed_ms_ = 0;
        SetTransient(EyeTransient::None);
        BeginTransition(exit_frame, ApplyActivityDelta(EmotionController::BasePose(intent_.emotion)));
    }
}

EyeAnimationCoordinator::Priority EyeAnimationCoordinator::CurrentPriority() const {
    if (intent_.activity == EyeActivity::Error) {
        return Priority::kError;
    }
    if (intent_.activity == EyeActivity::Sleeping || intent_.activity == EyeActivity::Waking) {
        return Priority::kSleepWake;
    }
    if (transient_ != EyeTransient::None) {
        return Priority::kInteraction;
    }
    if (intent_.activity == EyeActivity::Listening || intent_.activity == EyeActivity::Thinking ||
        intent_.activity == EyeActivity::Speaking) {
        return Priority::kSpeakingListeningThinking;
    }
    return Priority::kIdle;
}

EyeFrame EyeAnimationCoordinator::Compose() const {
    switch (transient_kind_) {
        case TransientKind::kPetting:
            return ComposePetting();
        case TransientKind::kStartled:
            return ComposeStartled();
        case TransientKind::kGroggyWake:
            return ComposeGroggy();
        case TransientKind::kNone:
        default:
            return ComposeSteadyState();
    }
}

bool EyeAnimationCoordinator::IdleAllowed() const {
    return intent_.activity == EyeActivity::Idle && transient_ == EyeTransient::None &&
           transient_kind_ == TransientKind::kNone;
}

bool EyeAnimationCoordinator::BlinkAllowed() const {
    // Matches legacy's exact suppression scope: GroggyWake only.
    // Petting/Startled are NOT suppressed in legacy either (extract,
    // don't restyle/expand).
    return intent_.blink_allowed && transient_kind_ != TransientKind::kGroggyWake;
}

void EyeAnimationCoordinator::BeginTransition(const EyeFrame& from, const EyeFrame& to) {
    transition_from_ = from;
    transition_to_ = to;
    transition_elapsed_ms_ = 0;
}

EyeFrame EyeAnimationCoordinator::ApplyActivityDelta(const EyeFrame& base) const {
    EyeFrame frame = base;
    switch (intent_.activity) {
        case EyeActivity::Thinking:
            frame.left.center_x += -12.0f;
            frame.right.center_x += -4.0f;
            frame.left.height += -2.0f;
            frame.right.height += -2.0f;
            break;
        case EyeActivity::Listening:
            frame.left.center_x += -4.0f;
            frame.right.center_x += 14.0f;
            frame.left.center_y += 5.0f;
            frame.right.center_y += 5.0f;
            frame.left.width += 10.0f;
            frame.right.width += 10.0f;
            frame.left.height += 22.0f;
            frame.right.height += 22.0f;
            break;
        default:
            // No activity delta for Idle/Speaking/Sleeping/Waking/Error/
            // Booting — only Thinking/Listening have a reviewed legacy
            // delta (09 Slice 7 scope).
            break;
    }
    return frame;
}

EyeFrame EyeAnimationCoordinator::ComposeSteadyState() const {
    const uint16_t progress = EyeAnimator::ClampProgress(transition_elapsed_ms_, kTransitionMs);
    return LerpFrame(transition_from_, transition_to_, progress);
}

EyeFrame EyeAnimationCoordinator::ComposePetting() const {
    const uint16_t progress = EyeAnimator::ClampProgress(transient_elapsed_ms_, kTransitionMs);
    const TransientPose from = FromEyeFrame(pet_anchor_);
    const TransientPose to = PettingBase();
    TransientPose pose = LerpTransientPose(from, to, progress);

    // Sway/bob: legacy uses frame_-count-based phases
    // ((frame_/5)%5-2, (frame_/8)%3-1) where frame_ is a Tick() call
    // counter, never reset. Converted here to a time-based equivalent
    // using transient_elapsed_ms_/kNominalTickMs as a pseudo-frame-count —
    // behavior-equivalent (same amplitude/period at the nominal 33ms tick
    // rate) but NOT bit-identical to legacy's true tick-driven counter,
    // since real LVGL tick delivery is not exactly 33ms and legacy's
    // counter never resets across the object's lifetime while this one is
    // relative to the pet's own start (09 Slice 8: "convert to time-based
    // equivalents... expected to be behavior-equivalent, not
    // bit-identical").
    const uint32_t pseudo_frame = transient_elapsed_ms_ / kNominalTickMs;
    const int sway = static_cast<int>((pseudo_frame / 5) % 5) - 2;
    const int bob = static_cast<int>((pseudo_frame / 8) % 3) - 1;
    pose.left_x += static_cast<float>(sway);
    pose.right_x += static_cast<float>(sway);
    pose.y += static_cast<float>(bob);

    // Opacity shimmer: same time-based conversion as sway/bob. No
    // per-frame randomness — purely a deterministic function of elapsed
    // time, matching legacy's deterministic (non-random) triangle wave.
    const int glow_phase = static_cast<int>((pseudo_frame / 4) % 8);
    const int glow_step = glow_phase <= 4 ? glow_phase : 8 - glow_phase;
    const float opacity = static_cast<float>(200 + glow_step * 13) / 255.0f;

    return ToEyeFrame(pose, opacity);
}

EyeFrame EyeAnimationCoordinator::ComposeStartled() const {
    // Bypasses activity delta entirely, matching legacy's mutually
    // exclusive transient branching (transients override, they do not
    // layer on top of activity adjustment) — ResolveBasePose(target_
    // emotion_) in legacy has no activity concept at all.
    const uint16_t progress = EyeAnimator::ClampProgress(transient_elapsed_ms_, kStartleDurationMs);
    const TransientPose emotion_base = FromEyeFrame(EmotionController::BasePose(intent_.emotion));
    const TransientPose from = StartledBase(emotion_base);
    const TransientPose pose = LerpTransientPose(from, emotion_base, progress);
    return ToEyeFrame(pose, 1.0f);
}

EyeFrame EyeAnimationCoordinator::ComposeGroggy() const {
    const uint16_t progress = EyeAnimator::ClampProgress(transient_elapsed_ms_, kGroggyDurationMs);
    const TransientPose sleeping = SleepingBase();
    const TransientPose sleepy = SleepyBase();
    const TransientPose neutral = NeutralBase();

    // Integer remapping, matching legacy's
    // `progress <= 300 ? progress*1000/300 : (progress-300)*1000/700`
    // exactly, including its truncation behavior — this selects WHICH
    // interpolation ratio applies and must stay integer to preserve the
    // documented boundary quirks (e.g. branch 2 does not truly begin
    // until elapsed_ms=1505, not 1501, due to truncation at the outer
    // progress computation).
    const uint16_t remapped =
        progress <= 300U
            ? static_cast<uint16_t>((static_cast<uint32_t>(progress) * 1000U) / 300U)
            : static_cast<uint16_t>(((static_cast<uint32_t>(progress) - 300U) * 1000U) / 700U);

    TransientPose pose = progress <= 300U ? LerpTransientPose(sleeping, sleepy, remapped)
                                           : LerpTransientPose(sleepy, neutral, remapped);

    // Pseudo-blink height clamp — matches legacy GroggyPose exactly,
    // including the quirk that the final rendered height stays clamped to
    // 40 (not Neutral's true 62) even at t=5000ms; full openness only
    // happens after this transient exits into the following transition
    // (see Update()'s GroggyWake auto-exit). Deliberately NOT "fixed" here
    // (09 Slice 8 scope).
    const uint16_t blink = progress < 150U ? static_cast<uint16_t>(1000U - progress * 4U) : 400U;
    // Integer division, then cast — matches legacy's
    // `static_cast<int>(blink / 10U)` truncation exactly, not a float
    // divide.
    const float blink_height_ceiling = static_cast<float>(blink / 10U);
    if (pose.height > blink_height_ceiling) {
        pose.height = blink_height_ceiling;
    }

    return ToEyeFrame(pose, 1.0f);
}

EyeFrame EyeAnimationCoordinator::LerpFrame(const EyeFrame& from, const EyeFrame& to,
                                             uint16_t progress_per_mille) {
    EyeFrame frame{};
    frame.left.center_x = EyeAnimator::LerpFloat(from.left.center_x, to.left.center_x, progress_per_mille);
    frame.left.center_y = EyeAnimator::LerpFloat(from.left.center_y, to.left.center_y, progress_per_mille);
    frame.left.width = EyeAnimator::LerpFloat(from.left.width, to.left.width, progress_per_mille);
    frame.left.height = EyeAnimator::LerpFloat(from.left.height, to.left.height, progress_per_mille);
    frame.left.corner_radius =
        EyeAnimator::LerpFloat(from.left.corner_radius, to.left.corner_radius, progress_per_mille);
    frame.left.rotation_degrees = EyeAnimator::LerpFloat(from.left.rotation_degrees,
                                                          to.left.rotation_degrees, progress_per_mille);
    frame.right.center_x =
        EyeAnimator::LerpFloat(from.right.center_x, to.right.center_x, progress_per_mille);
    frame.right.center_y =
        EyeAnimator::LerpFloat(from.right.center_y, to.right.center_y, progress_per_mille);
    frame.right.width = EyeAnimator::LerpFloat(from.right.width, to.right.width, progress_per_mille);
    frame.right.height = EyeAnimator::LerpFloat(from.right.height, to.right.height, progress_per_mille);
    frame.right.corner_radius =
        EyeAnimator::LerpFloat(from.right.corner_radius, to.right.corner_radius, progress_per_mille);
    frame.right.rotation_degrees = EyeAnimator::LerpFloat(
        from.right.rotation_degrees, to.right.rotation_degrees, progress_per_mille);
    frame.opacity = EyeAnimator::LerpFloat(from.opacity, to.opacity, progress_per_mille);
    return frame;
}
