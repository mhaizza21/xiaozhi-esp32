#include "eye_animation_coordinator.h"

void EyeAnimationCoordinator::SetIntent(const EyeIntent& intent) {
    intent_ = intent;
}

void EyeAnimationCoordinator::SetTransient(EyeTransient transient) {
    transient_ = transient;
}

void EyeAnimationCoordinator::Update(uint32_t delta_ms) {
    (void)delta_ms;
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
    // Idle and Booting both rank here: Booting is excluded from idle
    // permission separately (IdleAllowed requires activity == Idle
    // exactly), so ranking it alongside Idle does not grant it idle
    // motion — it only means neither preempts the other in this ranking.
    return Priority::kIdle;
}

EyeFrame EyeAnimationCoordinator::Compose(const EyeFrame& emotion_base_frame) const {
    EyeFrame frame = emotion_base_frame;
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
            // delta (09 Slice 7 scope); Speaking's own legacy geometry
            // difference (kSpeaking: eye_y+2, height 54) is a plausible
            // future candidate but was not part of this slice's reviewed
            // delta set and is intentionally left unimplemented.
            break;
    }
    return frame;
}

bool EyeAnimationCoordinator::IdleAllowed() const {
    return intent_.activity == EyeActivity::Idle && transient_ == EyeTransient::None;
}

bool EyeAnimationCoordinator::BlinkAllowed() const {
    return intent_.blink_allowed;
}
