#include "mhaibot_behavior_model.h"

#include <cassert>

MhaiBotBehaviorInput Input(DeviceState state, const char* emotion, bool groggy_wake_active,
                           bool servo_available) {
    MhaiBotBehaviorInput input{};
    input.state = state;
    input.emotion = emotion;
    input.groggy_wake_active = groggy_wake_active;
    input.servo_available = servo_available;
    return input;
}

int main() {
    const MhaiBotBehaviorModel model;

    // Boot/setup states must keep future servo output disabled.
    MhaiBotBehaviorIntent intent =
        model.FromInput(Input(kDeviceStateStarting, "neutral", false, true));
    assert(intent.eye.activity == EyeActivity::Booting);
    assert(intent.motion == MhaiBotMotionMode::BootStill);
    assert(!intent.servo_output_allowed);

    // Idle is centered and may be handed to a servo transport only after the
    // caller explicitly confirms that the controller is available.
    intent = model.FromInput(Input(kDeviceStateIdle, "neutral", false, false));
    assert(intent.eye.activity == EyeActivity::Idle);
    assert(intent.motion == MhaiBotMotionMode::IdleHold);
    assert(intent.neck_x == 0.0f);
    assert(intent.neck_y == 0.0f);
    assert(!intent.servo_output_allowed);

    intent = model.FromInput(Input(kDeviceStateIdle, "neutral", false, true));
    assert(intent.servo_output_allowed);

    // The app enters Listening by setting status LISTENING and emotion neutral;
    // the behavior model must preserve Listening instead of falling back idle.
    intent = model.FromInput(Input(kDeviceStateListening, "neutral", false, true));
    assert(intent.eye.activity == EyeActivity::Listening);
    assert(intent.motion == MhaiBotMotionMode::ListeningHold);
    assert(intent.servo_output_allowed);

    // Thinking from idle/listening gets a small normalized eye/neck tilt.
    intent = model.FromInput(Input(kDeviceStateIdle, "thinking", false, true));
    assert(intent.eye.activity == EyeActivity::Thinking);
    assert(intent.eye.emotion == EyeEmotion::Focused);
    assert(intent.motion == MhaiBotMotionMode::ThinkingTilt);
    assert(intent.eye.look_x < 0.0f);
    assert(intent.eye.look_y < 0.0f);
    assert(intent.neck_x < 0.0f);
    assert(intent.neck_y < 0.0f);
    assert(intent.servo_output_allowed);

    // Speaking may later become a gentle nod, but remains a normalized intent.
    intent = model.FromInput(Input(kDeviceStateSpeaking, "happy", false, true));
    assert(intent.eye.activity == EyeActivity::Speaking);
    assert(intent.eye.emotion == EyeEmotion::Happy);
    assert(intent.motion == MhaiBotMotionMode::SpeakingNod);
    assert(intent.neck_y > 0.0f);
    assert(intent.servo_output_allowed);

    // Sleep, groggy wake, and fatal error are hard stops for future servo
    // output even if the transport reports that a controller exists.
    intent = model.FromInput(Input(kDeviceStateIdle, "sleeping", false, true));
    assert(intent.eye.activity == EyeActivity::Sleeping);
    assert(intent.motion == MhaiBotMotionMode::SleepPose);
    assert(!intent.servo_output_allowed);
    assert(!intent.eye.blink_allowed);

    intent = model.FromInput(Input(kDeviceStateIdle, "neutral", true, true));
    assert(intent.eye.activity == EyeActivity::Waking);
    assert(intent.motion == MhaiBotMotionMode::WakingHold);
    assert(!intent.servo_output_allowed);

    intent = model.FromInput(Input(kDeviceStateFatalError, "neutral", false, true));
    assert(intent.eye.activity == EyeActivity::Error);
    assert(intent.motion == MhaiBotMotionMode::ErrorStop);
    assert(!intent.servo_output_allowed);
    assert(!intent.eye.blink_allowed);

    return 0;
}
