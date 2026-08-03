#ifndef _SERVO_BRIDGE_H_
#define _SERVO_BRIDGE_H_

#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <string>

// Non-blocking bridge to the ESP32-C3 dual-SG90 head controller
// (mhaibot-servo-c3) over a dedicated hardware UART.
//
// Wire protocol (115200 8N1, newline-terminated ASCII), see
// mhaibot-servo-c3/README.md:
//   -> PING            <- PONG
//   -> CENTER          <- OK,CENTER | ERR,*
//   -> MOVE,<pan>,<tilt> <- OK,MOVE,<pan>,<tilt> | ERR,*
//   (unsolicited)      <- READY   (sent once after the C3 boots/resets)
//
// All UART I/O, line parsing, link timeout/reconnect, and gesture
// animation run on a single dedicated low-priority FreeRTOS task, so
// this class never blocks the caller (audio, display, network, or MCP
// dispatch). Public methods only enqueue intent and return immediately.
//
// This is intentionally board-scoped for the first integration: the
// protocol/state-machine logic below (ProcessLine, EnqueueCommand, the
// timeout/reconnect and nod state machines) does not reference any
// board-specific type and can be lifted into a common component later
// without change, once a second board needs the same link.
class ServoBridge {
public:
    enum class Result {
        kSent,          // command accepted and queued for the link task
        kNotReady,      // C3 has not announced READY yet (or link timed out)
        kInvalidRange,  // pan/tilt outside the C3's configured safe range
    };

    static constexpr int kPanMin = 30;
    static constexpr int kPanMax = 150;
    static constexpr int kTiltMin = 50;
    static constexpr int kTiltMax = 120;
    static constexpr int kCenterPan = 90;
    static constexpr int kCenterTilt = 90;

    ServoBridge();
    ~ServoBridge();

    ServoBridge(const ServoBridge&) = delete;
    ServoBridge& operator=(const ServoBridge&) = delete;

    // Installs the UART driver on the pins from config.h and starts the
    // background task. Call once during board bring-up.
    void Initialize();

    // True once READY (or a subsequent PONG/OK/ERR reply) has been seen
    // recently; false before the first READY and after a link timeout,
    // until the C3 reconnects (sends an unsolicited READY again).
    bool IsReady() const;

    // Enqueues PING. Non-blocking; does not wait for PONG. Safe to call
    // regardless of link state (used internally as the keepalive probe).
    void Ping();

    Result CenterHead();
    Result MoveHead(int pan, int tilt);

    // Starts a short, non-blocking head-nod gesture (tilt-only). No-op
    // (still returns kSent) if a nod is already in progress.
    Result Nod();

private:
    enum class LinkState {
        kAwaitingReady,
        kReady,
        kTimedOut,
    };

    struct PendingCommand {
        char text[24];
    };

    struct NodStep {
        int tilt_delta;
        int hold_ms;
    };

    static void TaskEntry(void* arg);
    void TaskLoop();

    void ProcessLine(const std::string& line, int64_t now_us);
    void DrainTxQueue();
    bool EnqueueCommand(const std::string& line);
    void UpdateTimeouts(int64_t now_us);
    void UpdateNod(int64_t now_us);
    void MaybeWarn(int64_t now_us, const char* message);

    const uart_port_t uart_port_;
    TaskHandle_t task_handle_ = nullptr;
    QueueHandle_t cmd_queue_ = nullptr;
    std::string rx_line_;

    // Written only by the link task; read from arbitrary caller tasks via
    // IsReady()/MoveHead()/CenterHead()/Nod(), hence atomic.
    std::atomic<LinkState> state_{LinkState::kAwaitingReady};
    int64_t last_rx_us_ = 0;
    int64_t last_ping_sent_us_ = 0;
    int64_t last_warn_us_ = 0;

    bool nod_active_ = false;
    size_t nod_step_ = 0;
    int64_t nod_next_step_us_ = 0;

    static const NodStep kNodSteps[];
    static const size_t kNodStepCount;
};

#endif  // _SERVO_BRIDGE_H_
