#include "mhaibot_servo_uart.h"

#include <esp_log.h>

#define TAG "MhaiBotServoUart"

namespace {
const char* MotionModeName(MhaiBotMotionMode motion) {
    switch (motion) {
        case MhaiBotMotionMode::BootStill:
            return "BootStill";
        case MhaiBotMotionMode::IdleHold:
            return "IdleHold";
        case MhaiBotMotionMode::ListeningHold:
            return "ListeningHold";
        case MhaiBotMotionMode::ThinkingTilt:
            return "ThinkingTilt";
        case MhaiBotMotionMode::SpeakingNod:
            return "SpeakingNod";
        case MhaiBotMotionMode::SleepPose:
            return "SleepPose";
        case MhaiBotMotionMode::WakingHold:
            return "WakingHold";
        case MhaiBotMotionMode::ErrorStop:
            return "ErrorStop";
    }
    return "Unknown";
}
}  // namespace

void MhaiBotServoUart::ApplyIntent(const MhaiBotBehaviorIntent& intent) {
    if (!has_logged_disabled_notice_) {
        has_logged_disabled_notice_ = true;
        ESP_LOGI(TAG, "Servo UART bridge disabled; behavior intents are log-only");
    }

    if (has_last_motion_ && last_motion_ == intent.motion) {
        return;
    }
    has_last_motion_ = true;
    last_motion_ = intent.motion;

    ESP_LOGI(TAG,
             "Log-only intent: motion=%s neck_milli=(%d, %d) servo_output_allowed=%s",
             MotionModeName(intent.motion), static_cast<int>(intent.neck_x * 1000.0f),
             static_cast<int>(intent.neck_y * 1000.0f),
             intent.servo_output_allowed ? "true" : "false");
}
