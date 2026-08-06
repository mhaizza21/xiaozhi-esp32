#include "eye/eye_activity_adapter.h"
#include "eye/eye_intent.h"

#include "device_state.h"

#include <cassert>

int main() {
    const EyeActivityAdapter adapter;

    // Base DeviceState -> EyeActivity map (ADR-002), neutral emotion, no
    // groggy override.
    struct BaseCase {
        DeviceState state;
        EyeActivity expected;
    };
    const BaseCase base_cases[] = {
        {kDeviceStateUnknown, EyeActivity::Idle},
        {kDeviceStateStarting, EyeActivity::Booting},
        {kDeviceStateWifiConfiguring, EyeActivity::Booting},
        {kDeviceStateActivating, EyeActivity::Booting},
        {kDeviceStateUpgrading, EyeActivity::Booting},
        {kDeviceStateIdle, EyeActivity::Idle},
        {kDeviceStateConnecting, EyeActivity::Idle},
        {kDeviceStateListening, EyeActivity::Listening},
        {kDeviceStateSpeaking, EyeActivity::Speaking},
        {kDeviceStateAudioTesting, EyeActivity::Idle},
        {kDeviceStateFatalError, EyeActivity::Error},
    };
    for (const auto& c : base_cases) {
        const EyeIntent intent = adapter.FromDeviceState(c.state, "neutral", false);
        assert(intent.activity == c.expected);
        assert(intent.emotion == EyeEmotion::Neutral);
    }

    // Emotion string -> EyeEmotion map (02 / ADR-002), Idle base activity.
    struct EmotionCase {
        const char* emotion;
        EyeEmotion expected;
    };
    const EmotionCase emotion_cases[] = {
        {"neutral", EyeEmotion::Neutral},
        {"robot_2", EyeEmotion::Neutral},
        {"speaking", EyeEmotion::Neutral},
        {"unrecognized_xyz", EyeEmotion::Neutral},
        {"happy", EyeEmotion::Happy},
        {"laughing", EyeEmotion::Happy},
        {"notification", EyeEmotion::Happy},
        {"excited", EyeEmotion::Happy},
        {"thinking", EyeEmotion::Focused},
        {"confused", EyeEmotion::Focused},
        {"warning", EyeEmotion::Focused},
        {"listening", EyeEmotion::Focused},
        {"confident", EyeEmotion::Focused},
        {"relaxed", EyeEmotion::Sleepy},
        {"sleepy", EyeEmotion::Sleepy},
        {"sleeping", EyeEmotion::Sleepy},
        {"sleep", EyeEmotion::Sleepy},
        {"sad", EyeEmotion::Sad},
        {"angry", EyeEmotion::Angry},
        {"surprised", EyeEmotion::Surprised},
    };
    for (const auto& c : emotion_cases) {
        const EyeIntent intent = adapter.FromDeviceState(kDeviceStateIdle, c.emotion, false);
        assert(intent.emotion == c.expected);
    }
    assert(adapter.FromDeviceState(kDeviceStateIdle, nullptr, false).emotion == EyeEmotion::Neutral);

    // Ordered activity overrides (ADR-002).

    // 1. Sleeping emotion overrides any base activity to Sleeping.
    assert(adapter.FromDeviceState(kDeviceStateIdle, "sleeping", false).activity ==
           EyeActivity::Sleeping);
    assert(adapter.FromDeviceState(kDeviceStateListening, "sleep", false).activity ==
           EyeActivity::Sleeping);

    // 2. Groggy wake overrides to Waking (and beats a stale "sleeping" cue
    // that hasn't cleared yet is not tested here; sleeping takes precedence
    // per the documented order when both are true).
    assert(adapter.FromDeviceState(kDeviceStateIdle, "neutral", true).activity ==
           EyeActivity::Waking);

    // 3. "thinking"/"confused" only override Idle or Listening base activity.
    assert(adapter.FromDeviceState(kDeviceStateIdle, "thinking", false).activity ==
           EyeActivity::Thinking);
    assert(adapter.FromDeviceState(kDeviceStateListening, "confused", false).activity ==
           EyeActivity::Thinking);
    assert(adapter.FromDeviceState(kDeviceStateSpeaking, "thinking", false).activity ==
           EyeActivity::Speaking);

    // 4. "listening" emotion cue forces Listening activity even from Idle.
    assert(adapter.FromDeviceState(kDeviceStateIdle, "listening", false).activity ==
           EyeActivity::Listening);

    // 5. "speaking" emotion never overrides activity; Connecting stays Idle
    // (locked ADR-002 cases).
    assert(adapter.FromDeviceState(kDeviceStateIdle, "speaking", false).activity ==
           EyeActivity::Idle);
    assert(adapter.FromDeviceState(kDeviceStateConnecting, "neutral", false).activity ==
           EyeActivity::Idle);
    // Locked case: Listening + "neutral" (the app's actual entry sequence)
    // still yields Listening via the base DeviceState map.
    assert(adapter.FromDeviceState(kDeviceStateListening, "neutral", false).activity ==
           EyeActivity::Listening);

    // blink_allowed defaults: false for Sleeping/Waking/Error/Booting, true
    // otherwise.
    assert(!adapter.FromDeviceState(kDeviceStateIdle, "sleeping", false).blink_allowed);
    assert(!adapter.FromDeviceState(kDeviceStateIdle, "neutral", true).blink_allowed);
    assert(!adapter.FromDeviceState(kDeviceStateFatalError, "neutral", false).blink_allowed);
    assert(!adapter.FromDeviceState(kDeviceStateStarting, "neutral", false).blink_allowed);
    assert(adapter.FromDeviceState(kDeviceStateIdle, "neutral", false).blink_allowed);
    assert(adapter.FromDeviceState(kDeviceStateListening, "neutral", false).blink_allowed);
    assert(adapter.FromDeviceState(kDeviceStateSpeaking, "neutral", false).blink_allowed);

    return 0;
}
