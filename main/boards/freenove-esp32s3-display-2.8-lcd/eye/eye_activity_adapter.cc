#include "eye_activity_adapter.h"

#include <cstring>

EyeIntent EyeActivityAdapter::FromDeviceState(DeviceState state, const char* emotion,
                                               bool groggy_wake_active) const {
    EyeIntent intent{};
    intent.emotion = MapEmotion(emotion);

    EyeActivity activity = BaseActivity(state);

    const bool is_sleeping_emotion =
        emotion != nullptr && (std::strcmp(emotion, "sleeping") == 0 || std::strcmp(emotion, "sleep") == 0);
    const bool is_thinking_emotion =
        emotion != nullptr && (std::strcmp(emotion, "thinking") == 0 || std::strcmp(emotion, "confused") == 0);
    const bool is_listening_emotion = emotion != nullptr && std::strcmp(emotion, "listening") == 0;

    // Ordered activity overrides (ADR-002); "speaking" never overrides
    // activity — Speaking activity comes from kDeviceStateSpeaking only.
    if (is_sleeping_emotion) {
        activity = EyeActivity::Sleeping;
    } else if (groggy_wake_active) {
        activity = EyeActivity::Waking;
    } else if (is_thinking_emotion &&
               (activity == EyeActivity::Idle || activity == EyeActivity::Listening)) {
        activity = EyeActivity::Thinking;
    } else if (is_listening_emotion) {
        activity = EyeActivity::Listening;
    }

    intent.activity = activity;
    intent.blink_allowed = !(activity == EyeActivity::Sleeping || activity == EyeActivity::Waking ||
                              activity == EyeActivity::Error || activity == EyeActivity::Booting);
    return intent;
}

EyeActivity EyeActivityAdapter::BaseActivity(DeviceState state) {
    switch (state) {
        case kDeviceStateStarting:
        case kDeviceStateWifiConfiguring:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
            return EyeActivity::Booting;
        case kDeviceStateListening:
            return EyeActivity::Listening;
        case kDeviceStateSpeaking:
            return EyeActivity::Speaking;
        case kDeviceStateFatalError:
            return EyeActivity::Error;
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
        case kDeviceStateConnecting:
        case kDeviceStateAudioTesting:
        default:
            return EyeActivity::Idle;
    }
}

EyeEmotion EyeActivityAdapter::MapEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return EyeEmotion::Neutral;
    }
    if (std::strcmp(emotion, "happy") == 0 || std::strcmp(emotion, "laughing") == 0 ||
        std::strcmp(emotion, "notification") == 0 || std::strcmp(emotion, "excited") == 0) {
        return EyeEmotion::Happy;
    }
    if (std::strcmp(emotion, "thinking") == 0 || std::strcmp(emotion, "confused") == 0 ||
        std::strcmp(emotion, "warning") == 0 || std::strcmp(emotion, "listening") == 0 ||
        std::strcmp(emotion, "confident") == 0) {
        return EyeEmotion::Focused;
    }
    if (std::strcmp(emotion, "relaxed") == 0 || std::strcmp(emotion, "sleepy") == 0 ||
        std::strcmp(emotion, "sleeping") == 0 || std::strcmp(emotion, "sleep") == 0) {
        return EyeEmotion::Sleepy;
    }
    if (std::strcmp(emotion, "sad") == 0) {
        return EyeEmotion::Sad;
    }
    if (std::strcmp(emotion, "angry") == 0) {
        return EyeEmotion::Angry;
    }
    if (std::strcmp(emotion, "surprised") == 0) {
        return EyeEmotion::Surprised;
    }
    // "neutral", "robot_2", "speaking" (LLM expression while Speaking), and
    // any unrecognized string default to Neutral (02 / ADR-002).
    return EyeEmotion::Neutral;
}
