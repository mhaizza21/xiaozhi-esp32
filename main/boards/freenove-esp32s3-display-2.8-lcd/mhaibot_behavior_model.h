#ifndef MHAIBOT_BEHAVIOR_MODEL_H
#define MHAIBOT_BEHAVIOR_MODEL_H

#include "device_state.h"
#include "eye/eye_activity_adapter.h"
#include "eye/eye_intent.h"

// Pure board-local behavior model for MhaiBot's face/neck intent.
//
// This is deliberately not a servo driver. It decides what the bot should
// intend to do from DeviceState + emotion, while later slices can translate
// the neck intent into UART commands for the separate ESP32-C3 servo controller.
enum class MhaiBotMotionMode {
    BootStill,
    IdleHold,
    ListeningHold,
    ThinkingTilt,
    SpeakingNod,
    SleepPose,
    WakingHold,
    ErrorStop,
};

struct MhaiBotBehaviorInput {
    DeviceState state = kDeviceStateUnknown;
    const char* emotion = nullptr;
    bool groggy_wake_active = false;

    // Keep false until a board-specific transport layer has confirmed that a
    // servo controller is present and safe to command.
    bool servo_available = false;
};

struct MhaiBotBehaviorIntent {
    EyeIntent eye{};
    MhaiBotMotionMode motion = MhaiBotMotionMode::IdleHold;

    // Normalized future neck target, not servo pulse widths:
    // x: -1 left to +1 right, y: -1 up to +1 down.
    float neck_x = 0.0f;
    float neck_y = 0.0f;

    // True only for states that may safely reach a future servo transport, and
    // only when the caller reports an available servo controller.
    bool servo_output_allowed = false;
};

class MhaiBotBehaviorModel {
public:
    MhaiBotBehaviorIntent FromInput(const MhaiBotBehaviorInput& input) const;

private:
    EyeActivityAdapter eye_adapter_;
};

#endif  // MHAIBOT_BEHAVIOR_MODEL_H
