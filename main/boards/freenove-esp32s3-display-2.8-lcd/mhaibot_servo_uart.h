#ifndef MHAIBOT_SERVO_UART_H
#define MHAIBOT_SERVO_UART_H

#include "mhaibot_behavior_model.h"

// Disabled-by-default bridge for the future ESP32-S3 -> ESP32-C3 servo link.
//
// This slice is intentionally log-only. It gives the Freenove board firmware a
// single place to hand behavior intent to a future UART transport without
// moving hardware while the bot is away from the bench.
class MhaiBotServoUart {
public:
    void ApplyIntent(const MhaiBotBehaviorIntent& intent);

private:
    bool has_logged_disabled_notice_ = false;
    MhaiBotMotionMode last_motion_ = MhaiBotMotionMode::IdleHold;
    bool has_last_motion_ = false;
};

#endif  // MHAIBOT_SERVO_UART_H
