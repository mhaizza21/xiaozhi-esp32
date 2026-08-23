#include "mhaibot_servo_uart.h"

#include <cstdio>
#include <cstring>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "MhaiBotServoUart"

namespace {
constexpr uart_port_t kServoUartPort = UART_NUM_1;
constexpr gpio_num_t kServoUartTxGpio = GPIO_NUM_43;
constexpr gpio_num_t kServoUartRxGpio = GPIO_NUM_44;
constexpr int kServoUartBaud = 115200;
constexpr int kServoUartBufferSize = 256;
constexpr const char* kCenterCommand = "move 1500 1500\n";
constexpr int kCenterUs = 1500;
constexpr int kLeftUs = 1350;
constexpr int kRightUs = 1650;
constexpr int kUpUs = 1400;
constexpr int kDownUs = 1600;
constexpr int kGestureStepDelayMs = 180;
constexpr int kSmoothStepDelayMs = 90;
constexpr int kLookHoldMs = 2500;
constexpr int kManualMotionGuardMs = 800;

int Approach(int current, int target, int step) {
    if (current < target) {
        const int next = current + step;
        return next > target ? target : next;
    }
    if (current > target) {
        const int next = current - step;
        return next < target ? target : next;
    }
    return current;
}

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

bool MhaiBotServoUart::IsEnabled() const {
    return true;
}

bool MhaiBotServoUart::EnsureInitialized() {
    if (initialized_) {
        return true;
    }

    const uart_config_t uart_config = {
        .baud_rate = kServoUartBaud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(kServoUartPort, kServoUartBufferSize, 0, 0, nullptr, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install servo UART driver: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_param_config(kServoUartPort, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure servo UART: %s", esp_err_to_name(err));
        return false;
    }

    err = uart_set_pin(kServoUartPort, kServoUartTxGpio, kServoUartRxGpio, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set servo UART pins: %s", esp_err_to_name(err));
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "Servo UART center-only mode enabled: TX=GPIO%d RX=GPIO%d baud=%d",
             static_cast<int>(kServoUartTxGpio), static_cast<int>(kServoUartRxGpio),
             kServoUartBaud);
    return true;
}

bool MhaiBotServoUart::SendMoveCommand(int x_us, int y_us, const char* reason) {
    if (!EnsureInitialized()) {
        return false;
    }

    char command[32];
    const int written = snprintf(command, sizeof(command), "move %d %d\n", x_us, y_us);
    if (written <= 0 || written >= static_cast<int>(sizeof(command))) {
        ESP_LOGE(TAG, "Failed to format servo command for %s", reason);
        return false;
    }

    uart_write_bytes(kServoUartPort, command, written);
    ESP_LOGI(TAG, "Servo UART command sent for %s: move %d %d", reason, x_us, y_us);
    current_x_us_ = x_us;
    current_y_us_ = y_us;
    return true;
}

bool MhaiBotServoUart::SendSmoothMoveCommand(int target_x_us, int target_y_us, const char* reason) {
    constexpr int kStepUs = 50;

    while (current_x_us_ != target_x_us || current_y_us_ != target_y_us) {
        const int next_x = Approach(current_x_us_, target_x_us, kStepUs);
        const int next_y = Approach(current_y_us_, target_y_us, kStepUs);
        if (!SendMoveCommand(next_x, next_y, reason)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(kSmoothStepDelayMs));
    }

    return true;
}

bool MhaiBotServoUart::SendLookAndReturnCommand(int target_x_us, int target_y_us, const char* reason) {
    BeginManualMotionWindow(kLookHoldMs + kManualMotionGuardMs + 2000);
    if (!SendSmoothMoveCommand(target_x_us, target_y_us, reason)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kLookHoldMs));
    return SendSmoothMoveCommand(kCenterUs, kCenterUs, "soft-return-center");
}

void MhaiBotServoUart::SendCenterCommand(MhaiBotMotionMode motion) {
    if (!EnsureInitialized()) {
        return;
    }

    uart_write_bytes(kServoUartPort, kCenterCommand, std::strlen(kCenterCommand));
    ESP_LOGI(TAG, "Center-only UART command sent for motion=%s: move 1500 1500",
             MotionModeName(motion));
    current_x_us_ = kCenterUs;
    current_y_us_ = kCenterUs;
}

bool MhaiBotServoUart::SendActionCommand(const std::string& action) {
    if (action == "center") {
        BeginManualMotionWindow(kManualMotionGuardMs);
        return SendSmoothMoveCommand(kCenterUs, kCenterUs, "center-soft");
    }
    if (action == "left") {
        return SendLookAndReturnCommand(kLeftUs, kCenterUs, "look-left");
    }
    if (action == "right") {
        return SendLookAndReturnCommand(kRightUs, kCenterUs, "look-right");
    }
    if (action == "up") {
        return SendLookAndReturnCommand(kCenterUs, kUpUs, "look-up");
    }
    if (action == "down") {
        return SendLookAndReturnCommand(kCenterUs, kDownUs, "look-down");
    }
    if (action == "shake") {
        BeginManualMotionWindow(kManualMotionGuardMs + 2000);
        if (!SendSmoothMoveCommand(kLeftUs, kCenterUs, "shake-left")) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(kGestureStepDelayMs));
        SendSmoothMoveCommand(kRightUs, kCenterUs, "shake-right");
        vTaskDelay(pdMS_TO_TICKS(kGestureStepDelayMs));
        SendSmoothMoveCommand(kLeftUs, kCenterUs, "shake-left");
        vTaskDelay(pdMS_TO_TICKS(kGestureStepDelayMs));
        return SendSmoothMoveCommand(kCenterUs, kCenterUs, "shake-center");
    }
    if (action == "nod") {
        BeginManualMotionWindow(kManualMotionGuardMs + 2000);
        if (!SendSmoothMoveCommand(kCenterUs, kUpUs, "nod-up")) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(kGestureStepDelayMs));
        SendSmoothMoveCommand(kCenterUs, kDownUs, "nod-down");
        vTaskDelay(pdMS_TO_TICKS(kGestureStepDelayMs));
        SendSmoothMoveCommand(kCenterUs, kUpUs, "nod-up");
        vTaskDelay(pdMS_TO_TICKS(kGestureStepDelayMs));
        return SendSmoothMoveCommand(kCenterUs, kCenterUs, "nod-center");
    }

    ESP_LOGW(TAG, "Unsupported servo action: %s", action.c_str());
    return false;
}

bool MhaiBotServoUart::ManualMotionActive() const {
    return esp_timer_get_time() < manual_motion_until_us_;
}

void MhaiBotServoUart::BeginManualMotionWindow(int duration_ms) {
    manual_motion_until_us_ = esp_timer_get_time() + static_cast<int64_t>(duration_ms) * 1000;
}

void MhaiBotServoUart::ApplyIntent(const MhaiBotBehaviorIntent& intent) {
    if (!has_logged_ready_notice_) {
        has_logged_ready_notice_ = true;
        ESP_LOGI(TAG, "Servo UART bridge is in human-like manual motion validation mode");
    }

    if (has_last_motion_ && last_motion_ == intent.motion) {
        return;
    }
    has_last_motion_ = true;
    last_motion_ = intent.motion;

    ESP_LOGI(TAG,
             "Behavior intent observed: motion=%s neck_milli=(%d, %d) servo_output_allowed=%s",
             MotionModeName(intent.motion), static_cast<int>(intent.neck_x * 1000.0f),
             static_cast<int>(intent.neck_y * 1000.0f),
             intent.servo_output_allowed ? "true" : "false");
    if (ManualMotionActive()) {
        ESP_LOGI(TAG, "Skipping behavior center command while manual neck motion is active");
        return;
    }
    SendCenterCommand(intent.motion);
}
