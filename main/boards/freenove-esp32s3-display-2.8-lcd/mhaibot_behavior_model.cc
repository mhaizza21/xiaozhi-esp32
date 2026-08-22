#include "mhaibot_behavior_model.h"

MhaiBotBehaviorIntent MhaiBotBehaviorModel::FromInput(const MhaiBotBehaviorInput& input) const {
    MhaiBotBehaviorIntent intent{};
    intent.eye = eye_adapter_.FromDeviceState(input.state, input.emotion, input.groggy_wake_active);

    switch (intent.eye.activity) {
        case EyeActivity::Booting:
            intent.motion = MhaiBotMotionMode::BootStill;
            break;
        case EyeActivity::Idle:
            intent.motion = MhaiBotMotionMode::IdleHold;
            break;
        case EyeActivity::Listening:
            intent.motion = MhaiBotMotionMode::ListeningHold;
            break;
        case EyeActivity::Thinking:
            intent.motion = MhaiBotMotionMode::ThinkingTilt;
            intent.eye.look_x = -0.10f;
            intent.eye.look_y = -0.05f;
            intent.neck_x = -0.12f;
            intent.neck_y = -0.06f;
            break;
        case EyeActivity::Speaking:
            intent.motion = MhaiBotMotionMode::SpeakingNod;
            intent.neck_y = 0.08f;
            break;
        case EyeActivity::Sleeping:
            intent.motion = MhaiBotMotionMode::SleepPose;
            break;
        case EyeActivity::Waking:
            intent.motion = MhaiBotMotionMode::WakingHold;
            break;
        case EyeActivity::Error:
        default:
            intent.motion = MhaiBotMotionMode::ErrorStop;
            break;
    }

    intent.servo_output_allowed =
        input.servo_available && intent.motion != MhaiBotMotionMode::BootStill &&
        intent.motion != MhaiBotMotionMode::SleepPose &&
        intent.motion != MhaiBotMotionMode::WakingHold &&
        intent.motion != MhaiBotMotionMode::ErrorStop;
    return intent;
}
