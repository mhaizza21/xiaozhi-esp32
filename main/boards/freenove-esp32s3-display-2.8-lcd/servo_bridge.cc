#include "servo_bridge.h"

#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <esp_timer.h>

#include "config.h"

#define TAG "ServoBridge"

namespace {
constexpr int64_t kLinkTimeoutUs = 3 * 1000 * 1000;
constexpr int64_t kPingIntervalUs = 2 * 1000 * 1000;
constexpr int64_t kWarnIntervalUs = 5 * 1000 * 1000;
constexpr size_t kMaxLineLength = 63;  // mirrors the C3's kMaxCommandLength
}  // namespace

const ServoBridge::NodStep ServoBridge::kNodSteps[] = {
    {18, 220},
    {-18, 220},
    {18, 220},
    {0, 220},
};
const size_t ServoBridge::kNodStepCount = sizeof(kNodSteps) / sizeof(kNodSteps[0]);

ServoBridge::ServoBridge() : uart_port_(SERVO_BRIDGE_UART_PORT) {}

// The UART driver, queue, and task are installed once in Initialize() and
// intentionally never torn down: this object lives for the process
// lifetime, matching how other board peripherals in this codebase are
// managed (e.g. the static Led/Backlight instances).
ServoBridge::~ServoBridge() = default;

void ServoBridge::Initialize() {
    uart_config_t uart_config = {};
    uart_config.baud_rate = SERVO_BRIDGE_UART_BAUD;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(uart_port_, 256, 256, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_port_, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_port_, SERVO_BRIDGE_UART_TX_PIN, SERVO_BRIDGE_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    cmd_queue_ = xQueueCreate(8, sizeof(PendingCommand));

    const int64_t now_us = esp_timer_get_time();
    last_rx_us_ = now_us;
    last_ping_sent_us_ = now_us;

    xTaskCreate(&ServoBridge::TaskEntry, "servo_bridge", 3072, this, tskIDLE_PRIORITY + 2,
                &task_handle_);
}

bool ServoBridge::IsReady() const {
    return state_.load(std::memory_order_relaxed) == LinkState::kReady;
}

void ServoBridge::Ping() { EnqueueCommand("PING"); }

ServoBridge::Result ServoBridge::CenterHead() {
    if (state_.load(std::memory_order_relaxed) != LinkState::kReady) {
        return Result::kNotReady;
    }
    EnqueueCommand("CENTER");
    return Result::kSent;
}

ServoBridge::Result ServoBridge::MoveHead(int pan, int tilt) {
    if (pan < kPanMin || pan > kPanMax || tilt < kTiltMin || tilt > kTiltMax) {
        return Result::kInvalidRange;
    }
    if (state_.load(std::memory_order_relaxed) != LinkState::kReady) {
        return Result::kNotReady;
    }
    char text[24];
    snprintf(text, sizeof(text), "MOVE,%d,%d", pan, tilt);
    EnqueueCommand(text);
    return Result::kSent;
}

ServoBridge::Result ServoBridge::Nod() {
    if (state_.load(std::memory_order_relaxed) != LinkState::kReady) {
        return Result::kNotReady;
    }
    if (!nod_active_) {
        nod_active_ = true;
        nod_step_ = 0;
        nod_next_step_us_ = esp_timer_get_time();
    }
    return Result::kSent;
}

void ServoBridge::TaskEntry(void* arg) { static_cast<ServoBridge*>(arg)->TaskLoop(); }

void ServoBridge::TaskLoop() {
    uint8_t rx_chunk[32];
    while (true) {
        int len = uart_read_bytes(uart_port_, rx_chunk, sizeof(rx_chunk), pdMS_TO_TICKS(20));
        const int64_t now_us = esp_timer_get_time();

        for (int i = 0; i < len; ++i) {
            const char c = static_cast<char>(rx_chunk[i]);
            if (c == '\n') {
                ProcessLine(rx_line_, now_us);
                rx_line_.clear();
            } else if (c == '\r') {
                continue;
            } else if (rx_line_.size() >= kMaxLineLength) {
                rx_line_.clear();
                MaybeWarn(now_us, "servo link: incoming line too long, dropped");
            } else {
                rx_line_.push_back(c);
            }
        }

        UpdateTimeouts(now_us);
        UpdateNod(now_us);
        DrainTxQueue();
    }
}

// Marks the link alive as of now_us and, only on an actual state change,
// logs the transition. This is the ONLY place last_rx_us_ advances, and it
// is only called for lines that parsed as a recognized protocol reply
// (READY/PONG/OK,*/ERR,*) — never for raw unrecognized bytes (e.g. the
// C3 ROM bootloader's boot banner, which is also physically received on
// this same UART since it shares pins with the C3's own UART0 before its
// app remaps them). Counting that banner as "alive" was the original bug:
// it kept resetting the liveness timer through every C3 reset, so a real
// C3 outage could never actually cross the timeout threshold.
void ServoBridge::MarkLinkAlive(int64_t now_us, const char* reason) {
    last_rx_us_ = now_us;
    const LinkState previous = state_.exchange(LinkState::kReady, std::memory_order_relaxed);
    if (previous != LinkState::kReady) {
        ESP_LOGI(TAG, "servo link: %s -> ready (%s)",
                 previous == LinkState::kAwaitingReady ? "awaiting_ready" : "timed_out", reason);
    }
}

void ServoBridge::ProcessLine(const std::string& line, int64_t now_us) {
    if (line.empty()) {
        return;
    }

    if (line == "READY") {
        MarkLinkAlive(now_us, "READY");
        return;
    }

    if (line == "PONG") {
        // A PONG only ever comes after the C3's setup() has already run
        // to completion (which is where it emits READY and attaches the
        // servos), so it is just as valid a "ready" signal as READY
        // itself. This is what lets the link recover after an S3-only
        // reset, where the already-booted C3 will never resend READY.
        MarkLinkAlive(now_us, "PONG");
        return;
    }

    if (line.rfind("OK,", 0) == 0) {
        MarkLinkAlive(now_us, "OK");
        return;
    }

    if (line.rfind("ERR,", 0) == 0) {
        // Still a well-formed protocol reply — the C3 is alive and parsed
        // a complete line, it just rejected the command — so this counts
        // as valid activity too, unlike the unrecognized-noise case below.
        MarkLinkAlive(now_us, "ERR");
        MaybeWarn(now_us, ("servo link: C3 reported " + line).c_str());
        return;
    }

    // Not a recognized protocol reply: most commonly the C3's own ROM
    // bootloader banner leaking onto this UART during a C3 reset (see the
    // comment on MarkLinkAlive), or electrical noise from a reset pulse.
    // Deliberately does not touch last_rx_us_ or state_.
    MaybeWarn(now_us, ("servo link: unrecognized reply (ignored, not counted as alive): " + line).c_str());
}

void ServoBridge::UpdateTimeouts(int64_t now_us) {
    if (state_.load(std::memory_order_relaxed) == LinkState::kReady &&
        (now_us - last_rx_us_) > kLinkTimeoutUs) {
        state_.store(LinkState::kTimedOut, std::memory_order_relaxed);
        // Transition logs are unconditional (not MaybeWarn-rate-limited):
        // this fires exactly once per ready->timed_out edge, so an unlucky
        // recent unrelated warning must never be able to swallow it.
        ESP_LOGW(TAG, "servo link: ready -> timed_out (no valid reply from C3 in over %lld ms)",
                 static_cast<long long>(kLinkTimeoutUs / 1000));
    }

    if ((now_us - last_ping_sent_us_) > kPingIntervalUs) {
        last_ping_sent_us_ = now_us;
        EnqueueCommand("PING");
    }
}

void ServoBridge::UpdateNod(int64_t now_us) {
    if (!nod_active_) {
        return;
    }
    if (now_us < nod_next_step_us_) {
        return;
    }
    if (nod_step_ >= kNodStepCount) {
        nod_active_ = false;
        return;
    }

    const NodStep& step = kNodSteps[nod_step_];
    int tilt = kCenterTilt + step.tilt_delta;
    if (tilt < kTiltMin)
        tilt = kTiltMin;
    if (tilt > kTiltMax)
        tilt = kTiltMax;

    char text[24];
    snprintf(text, sizeof(text), "MOVE,%d,%d", kCenterPan, tilt);
    EnqueueCommand(text);

    nod_next_step_us_ = now_us + static_cast<int64_t>(step.hold_ms) * 1000;
    ++nod_step_;
}

bool ServoBridge::EnqueueCommand(const std::string& line) {
    PendingCommand cmd{};
    snprintf(cmd.text, sizeof(cmd.text), "%s", line.c_str());
    if (xQueueSend(cmd_queue_, &cmd, 0) != pdTRUE) {
        MaybeWarn(esp_timer_get_time(), "servo link: command queue full, dropping command");
        return false;
    }
    return true;
}

void ServoBridge::DrainTxQueue() {
    PendingCommand cmd;
    while (xQueueReceive(cmd_queue_, &cmd, 0) == pdTRUE) {
        const size_t len = strnlen(cmd.text, sizeof(cmd.text));
        uart_write_bytes(uart_port_, cmd.text, len);
        uart_write_bytes(uart_port_, "\n", 1);
    }
}

void ServoBridge::MaybeWarn(int64_t now_us, const char* message) {
    if (now_us - last_warn_us_ < kWarnIntervalUs) {
        return;
    }
    last_warn_us_ = now_us;
    ESP_LOGW(TAG, "%s", message);
}
