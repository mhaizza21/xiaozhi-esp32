#ifndef MHAIBOT_SERVO_UART_H
#define MHAIBOT_SERVO_UART_H

#include "mhaibot_behavior_model.h"

#include <string>

// ESP32-S3 -> ESP32-C3 servo link.
//
// Behavior intent remains center-only, while the explicit MCP tool can request
// small bounded neck movements for hardware validation.
class MhaiBotServoUart {
public:
    bool IsEnabled() const;
    void ApplyIntent(const MhaiBotBehaviorIntent& intent);
    bool SendActionCommand(const std::string& action);

private:
    bool EnsureInitialized();
    void SendCenterCommand(MhaiBotMotionMode motion);
    bool SendMoveCommand(int x_us, int y_us, const char* reason);
    bool SendSmoothMoveCommand(int target_x_us, int target_y_us, const char* reason);
    bool SendLookAndReturnCommand(int target_x_us, int target_y_us, const char* reason);
    bool ManualMotionActive() const;
    void BeginManualMotionWindow(int duration_ms);

    bool initialized_ = false;
    bool has_logged_ready_notice_ = false;
    MhaiBotMotionMode last_motion_ = MhaiBotMotionMode::IdleHold;
    bool has_last_motion_ = false;
    int current_x_us_ = 1500;
    int current_y_us_ = 1500;
    int64_t manual_motion_until_us_ = 0;
};

#endif  // MHAIBOT_SERVO_UART_H
