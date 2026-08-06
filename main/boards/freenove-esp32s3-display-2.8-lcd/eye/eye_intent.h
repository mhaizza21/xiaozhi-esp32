#ifndef MHAIBOT_EYE_INTENT_H
#define MHAIBOT_EYE_INTENT_H

// Canonical eye-animation dimensions (05 §3, ADR-001). Activity, emotion,
// and transient are orthogonal — not a single flat enum.

enum class EyeActivity {
    Booting,
    Idle,
    Listening,
    Thinking,
    Speaking,
    Sleeping,
    Waking,
    Error,
};

enum class EyeEmotion {
    Neutral,
    Happy,
    Sad,
    Angry,
    Surprised,
    Focused,
    Sleepy,
};

enum class EyeTransient {
    None,
    Blink,
    DoubleBlink,
    Glance,
    TouchReaction,
};

// Structured intent snapshot (02). Producers publish a complete struct via
// EyeIntentMailbox; the display owner consumes the latest value.
struct EyeIntent {
    EyeActivity activity = EyeActivity::Idle;
    EyeEmotion emotion = EyeEmotion::Neutral;
    float look_x = 0.0f;  // -1 left … +1 right
    float look_y = 0.0f;  // -1 up … +1 down
    bool blink_allowed = true;
};

#endif  // MHAIBOT_EYE_INTENT_H
