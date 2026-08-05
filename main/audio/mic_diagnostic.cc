#include "mic_diagnostic.h"

#include "sdkconfig.h"

#if CONFIG_MIC_DIAGNOSTIC

#include <errno.h>
#include <unistd.h>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>

// Diagnostic-only cross-module probe (see its own header for the full
// rationale): lets LogEnomemContext() below report whether production
// WebsocketProtocol audio traffic was recently active. Not a new
// dependency direction -- mic_diagnostic.cc already reads project-wide
// state cross-module (e.g. wifi_manager.h below); this follows the same
// pattern for the investigation this file exists to support.
#include "network_activity_probe.h"

// Only used for the StartNetworkMonitoring() fallback check (has Wi-Fi
// already gotten an IP before we registered?) — the project's own
// abstraction, not a new dependency invented for this fix. Already used
// elsewhere in main/ (e.g. wifi_board.cc), so its include path is already
// available to this component.
#include <wifi_manager.h>

#define TAG "MicDiagnostic"

// ---------------------------------------------------------------------------
// Design summary (see mic_diagnostic.h for the threading contract and the
// full state-machine writeup):
//
//   AudioInputTask (producer, priority 8, core 0 when CONFIG_USE_AUDIO_PROCESSOR)
//       ReadAudioData()
//         codec_->InputData(data)              <- raw 24kHz
//         TapRaw(...)                          <- observes audio-ready / counts
//                                                  warm-up / pushes to ring,
//                                                  depending on state_
//         [resample 24k -> 16k]
//         TapResampled(...)                    <- follows state_ (never makes
//                                                  its own warm-up decision)
//
//   MicDiagnostic::SenderTask (consumer, priority tskIDLE_PRIORITY+1, core 1)
//       loop: drive kWaitingFor*->kWarmingUp, notice kCapturing (administer
//             capture start), FrameRing::TryPop both rings -> serialize ->
//             sendto()
//
// Both FrameRing<> instances are classic single-producer/single-consumer
// lock-free ring buffers over atomic head/tail counters: the producer only
// ever writes head_, the consumer only ever writes tail_, and each side
// only reads the other's counter with acquire ordering before touching
// slot data. Because it's strictly SPSC, a slot can never be written by
// the producer while the consumer is still reading the same slot: the
// producer is only allowed to reuse slot N again once tail_ has advanced
// past the point where slot N was last consumed, which by construction
// only happens after TryPop for that slot has returned. No mutex, no
// critical section, anywhere in this file.
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t kMagic = 0x4D494344;  // 'MICD'
constexpr uint16_t kProtocolVersion = 1;
constexpr size_t kHeaderLength = 50;
constexpr size_t kMaxPayloadBytes =
    256 * sizeof(int16_t);  // matches kRawMaxSamples/kResampledMaxSamples
constexpr size_t kMaxPacketBytes = kHeaderLength + kMaxPayloadBytes;
// Conservative bound well under the ~1500-byte Ethernet MTU, so a DATA
// packet (header + payload) never risks IP fragmentation. Checked
// explicitly at send time, not just assumed from kMaxPacketBytes.
constexpr size_t kMaxDatagramBytes = 1400;

// SenderTaskLoop()'s poll interval between ring-drain passes. This project
// builds with CONFIG_FREERTOS_HZ=100 (10ms tick period), under which
// pdMS_TO_TICKS(5) truncates via integer division to 0 ticks --
// vTaskDelay(0) is a bare yield, not a real delay, which made SenderTask
// spin at scheduler speed (~56,000 iterations/sec, measured) instead of
// sleeping, periodically starving IDLE1 on core 1 and tripping the Task
// Watchdog (confirmed on real hardware, Phase D1.1). Guarantee at least 1
// real tick regardless of the configured tick rate.
constexpr TickType_t kSenderDelayTicks = pdMS_TO_TICKS(5) > 0 ? pdMS_TO_TICKS(5) : 1;

enum : uint8_t {
    kFlagStart = 0x01,
    kFlagData = 0x02,
    kFlagEnd = 0x04,
};

inline void WriteU8(uint8_t* p, uint8_t v) { p[0] = v; }
inline void WriteU16BE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
inline void WriteU32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}
inline void WriteU64BE(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>(v >> (56 - i * 8));
    }
}

// Header layout (all multi-byte integer fields big-endian / network byte
// order; the PCM payload that follows is raw little-endian int16 samples,
// i.e. ESP32's native in-memory layout, copied verbatim with no per-sample
// conversion, so a receiver can write it straight into a WAV data chunk):
//   u32 magic            u16 protocol_version   u16 header_length
//   u8  stream_id        u8  flags              u32 capture_id
//   u32 source_frame_id  u32 sequence_number    u64 first_sample_index
//   u32 sample_count     u64 monotonic_timestamp_us
//   u32 payload_length   u32 dropped_frame_count_snapshot
// = 50 bytes, matches kHeaderLength.
// Shared by TapRaw/TapResampled's warm-up branches: accumulates peak
// (max abs sample) and sum-of-squares (for RMS) over one already-in-hand
// frame. Pure arithmetic over data the caller already has — no
// allocation, no I/O — safe to call from AudioInputTask.
void AccumulatePeakSumSq(const int16_t* samples, size_t sample_count, int32_t& peak_out,
                         int64_t& sum_sq_out) {
    int32_t peak = 0;
    int64_t sum_sq = 0;
    for (size_t i = 0; i < sample_count; ++i) {
        const int32_t v = samples[i];
        const int32_t av = v < 0 ? -v : v;
        if (av > peak) {
            peak = av;
        }
        sum_sq += static_cast<int64_t>(v) * v;
    }
    peak_out = peak;
    sum_sq_out = sum_sq;
}

// RMS from an accumulated sum-of-squares and sample count, computed once
// at log time (not per-frame), so a floating-point sqrt here is cheap and
// harmless. 0 samples -> 0.0 rather than NaN/div-by-zero.
double WarmupRms(int64_t sum_sq, uint32_t sample_count) {
    if (sample_count == 0) {
        return 0.0;
    }
    return std::sqrt(static_cast<double>(sum_sq) / static_cast<double>(sample_count));
}

size_t SerializeHeader(uint8_t* buf, uint16_t stream_id, uint8_t flags, uint32_t capture_id,
                       uint32_t source_frame_id, uint32_t sequence_number,
                       uint64_t first_sample_index, uint32_t sample_count, uint64_t timestamp_us,
                       uint32_t payload_length, uint32_t dropped_count) {
    size_t off = 0;
    WriteU32BE(buf + off, kMagic);
    off += 4;
    WriteU16BE(buf + off, kProtocolVersion);
    off += 2;
    WriteU16BE(buf + off, static_cast<uint16_t>(kHeaderLength));
    off += 2;
    WriteU8(buf + off, static_cast<uint8_t>(stream_id));
    off += 1;
    WriteU8(buf + off, flags);
    off += 1;
    WriteU32BE(buf + off, capture_id);
    off += 4;
    WriteU32BE(buf + off, source_frame_id);
    off += 4;
    WriteU32BE(buf + off, sequence_number);
    off += 4;
    WriteU64BE(buf + off, first_sample_index);
    off += 8;
    WriteU32BE(buf + off, sample_count);
    off += 4;
    WriteU64BE(buf + off, timestamp_us);
    off += 8;
    WriteU32BE(buf + off, payload_length);
    off += 4;
    WriteU32BE(buf + off, dropped_count);
    off += 4;
    return off;  // == kHeaderLength
}

}  // namespace

// ---- FrameRing<> ------------------------------------------------------

template <size_t kMaxSamples, size_t kCapacity>
bool MicDiagnostic::FrameRing<kMaxSamples, kCapacity>::Init() {
    slots_ = static_cast<Frame*>(heap_caps_malloc(sizeof(Frame) * kCapacity, MALLOC_CAP_SPIRAM));
    return slots_ != nullptr;
}

template <size_t kMaxSamples, size_t kCapacity>
bool MicDiagnostic::FrameRing<kMaxSamples, kCapacity>::TryPush(uint32_t source_frame_id,
                                                               uint64_t first_sample_index,
                                                               int64_t timestamp_us,
                                                               const int16_t* samples,
                                                               size_t sample_count) {
    if (sample_count > kMaxSamples) {
        dropped_frame_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= kCapacity) {
        dropped_frame_count_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    Frame& slot = slots_[head % kCapacity];
    slot.source_frame_id = source_frame_id;
    slot.first_sample_index = first_sample_index;
    slot.timestamp_us = timestamp_us;
    slot.sample_count = static_cast<uint16_t>(sample_count);
    memcpy(slot.samples, samples, sample_count * sizeof(int16_t));

    head_.store(head + 1, std::memory_order_release);

    // Single-writer field (see declaration): plain bounded read-then-write,
    // no compare_exchange retry loop — there is no other task that could
    // write high_water_mark_ between the load and the store below.
    const uint32_t depth = head + 1 - tail;
    if (depth > high_water_mark_.load(std::memory_order_relaxed)) {
        high_water_mark_.store(depth, std::memory_order_relaxed);
    }
    return true;
}

template <size_t kMaxSamples, size_t kCapacity>
bool MicDiagnostic::FrameRing<kMaxSamples, kCapacity>::TryPop(Frame& out) {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    const uint32_t head = head_.load(std::memory_order_acquire);
    if (head == tail) {
        return false;
    }
    out = slots_[tail % kCapacity];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}

template <size_t kMaxSamples, size_t kCapacity>
void MicDiagnostic::FrameRing<kMaxSamples, kCapacity>::ResetCounters() {
    dropped_frame_count_.store(0, std::memory_order_relaxed);
    high_water_mark_.store(0, std::memory_order_relaxed);
}

// ---- MicDiagnostic ------------------------------------------------------

MicDiagnostic& MicDiagnostic::GetInstance() {
    static MicDiagnostic instance;
    return instance;
}

void MicDiagnostic::Initialize() {
    // One-time static validation of the wire format: SerializeHeader's
    // sequence of writes is fixed for every call, so if this self-test
    // passes once with dummy values it holds for every real call too —
    // no need to re-check header length per packet. payload_length and
    // header+payload-vs-datagram-budget ARE re-checked per packet in
    // DrainRawRing/DrainResampledRing, since those genuinely vary per
    // frame.
    uint8_t self_test_packet[kHeaderLength];
    const size_t self_test_len =
        SerializeHeader(self_test_packet, kStreamRaw24k, kFlagData, 0, 0, 0, 0, 0, 0, 0, 0);
    if (self_test_len != kHeaderLength) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=header_self_test_failed wrote=%u expected=%u",
                 static_cast<unsigned>(self_test_len), static_cast<unsigned>(kHeaderLength));
        return;
    }

    if (!raw_ring_.Init()) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=raw_ring_alloc_failed");
        return;
    }
    if (!resampled_ring_.Init()) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=resampled_ring_alloc_failed");
        return;
    }

    // No socket(), no inet_pton(), no network-dependent timer here: this
    // function runs from AudioService::Initialize(), which is called
    // before board.StartNetwork() has run esp_netif_init(). lwIP's tcpip
    // task does not exist yet at this point, so any BSD socket call here
    // would hit lwIP's own "Invalid mbox" assert and reboot the board (see
    // the CreateSocket()/OnGotIp() comments for where that work now
    // happens instead — deferred to SenderTask, gated on network_ready_).

    BaseType_t task_created =
        xTaskCreatePinnedToCore(&MicDiagnostic::SenderTaskEntry, "mic_diag_sender", 4096, this,
                                tskIDLE_PRIORITY + 1, &sender_task_handle_, 1);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=sender_task_create_failed");
        return;
    }

    // No more arm_timer_: the old design armed a fixed 3000ms-after-GOT_IP
    // one-shot timer, which raced the production audio pipeline's own,
    // independently-timed startup (confirmed on real hardware: the gap
    // between GOT_IP and the first real PCM frame ranged from ~70ms to
    // ~2.26s across runs, depending on OTA/MQTT latency, unrelated to
    // Wi-Fi timing at all). Capture now starts precisely when TapRaw
    // observes the warm-up sample target crossed — see the state-machine
    // doc in mic_diagnostic.h.
    esp_timer_create_args_t end_timer_args = {};
    end_timer_args.callback = &MicDiagnostic::CaptureEndTimerCallback;
    end_timer_args.arg = this;
    end_timer_args.name = "mic_diag_end";
    if (esp_timer_create(&end_timer_args, &capture_end_timer_) != ESP_OK) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=end_timer_create_failed");
        return;
    }

    // No esp_event_handler_register() here either — same root cause as the
    // socket-ordering issue above. The default event loop doesn't exist
    // yet at this point (esp_event_loop_create_default() only runs later,
    // inside WifiManager::Initialize() -> board.StartNetwork()), so
    // registering here would fail with ESP_ERR_INVALID_STATE every time
    // (confirmed on real hardware). Event registration is deferred to
    // StartNetworkMonitoring(), called separately once board.StartNetwork()
    // has returned.

    initialized_ = true;
    ESP_LOGI(TAG, "MIC_DIAG_INIT network_deferred=1 t_us=%lld",
             static_cast<long long>(esp_timer_get_time()));
}

void MicDiagnostic::StartNetworkMonitoring() {
    if (!initialized_) {
        return;  // Initialize() already failed and logged why; stay fully inert
    }
    if (network_monitor_started_.exchange(true, std::memory_order_acq_rel)) {
        return;  // idempotent: already registered (or already tried and failed)
    }

    const esp_err_t event_register_err = esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &MicDiagnostic::GotIpHandler, this);
    if (event_register_err != ESP_OK) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=event_handler_register_failed err=%d (%s)",
                 event_register_err, esp_err_to_name(event_register_err));
        // network_ready_ can now never become true, but SenderTaskLoop's
        // poll is a cheap, real-delay (see kSenderDelayTicks) check, not a
        // busy-loop, so this is a harmless, permanent no-op rather than a
        // wasteful spin.
        return;
    }
    ESP_LOGI(TAG, "MIC_DIAG_NETWORK_MONITOR_REGISTERED base=IP_EVENT id=IP_EVENT_STA_GOT_IP t_us=%lld",
             static_cast<long long>(esp_timer_get_time()));

    // Fallback for the race where Wi-Fi already had an IP before we got a
    // chance to register above — IP_EVENT_STA_GOT_IP is not redelivered
    // retroactively, so without this check that boot would never see
    // network_ready_ become true at all. WifiManager::IsConnected() is
    // true iff WifiStation's own IP_EVENT_STA_GOT_IP handler has already
    // fired at least once (verified: it sets the same event-group bit
    // IsConnected() reads, inside that handler — see wifi_station.cc).
    if (WifiManager::GetInstance().IsConnected()) {
        const bool was_ready = network_ready_.exchange(true, std::memory_order_acq_rel);
        if (!was_ready) {
            ESP_LOGI(TAG, "MIC_DIAG_NETWORK_ALREADY_READY t_us=%lld",
                     static_cast<long long>(esp_timer_get_time()));
        }
    }
}

bool MicDiagnostic::CreateSocket() {
    if (udp_sockfd_ >= 0) {
        return true;  // idempotent: already created, never opened twice
    }
    create_socket_attempt_count_.fetch_add(1, std::memory_order_relaxed);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=socket_create_failed errno=%d", errno);
        return false;
    }

    std::string server_addr = CONFIG_MIC_DIAGNOSTIC_UDP_SERVER;
    size_t colon_pos = server_addr.find(':');
    if (colon_pos == std::string::npos) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=bad_server_address value=%s", server_addr.c_str());
        close(fd);
        return false;
    }
    std::string ip = server_addr.substr(0, colon_pos);
    int port = 0;
    try {
        port = std::stoi(server_addr.substr(colon_pos + 1));
    } catch (const std::exception&) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=bad_server_port value=%s", server_addr.c_str());
        close(fd);
        return false;
    }

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        ESP_LOGE(TAG, "MIC_DIAG_DISABLED reason=bad_server_ip value=%s", ip.c_str());
        close(fd);
        return false;
    }

    udp_server_addr_ = addr;
    udp_sockfd_ = fd;
    create_socket_success_count_.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGI(TAG, "MIC_DIAG_SOCKET_CREATED fd=%d server=%s t_us=%lld", fd,
             CONFIG_MIC_DIAGNOSTIC_UDP_SERVER, static_cast<long long>(esp_timer_get_time()));
    return true;
}

void MicDiagnostic::GotIpHandler(void* arg, esp_event_base_t /*base*/, int32_t /*event_id*/,
                                 void* /*event_data*/) {
    static_cast<MicDiagnostic*>(arg)->OnGotIp();
}

void MicDiagnostic::OnGotIp() {
    // System-event-task context: must stay fast and non-blocking, so this
    // only flips a flag — no socket(), no sendto(), no timer start here.
    // SenderTask polls network_ready_ and does the real work (CreateSocket,
    // then the state-machine transition once audio is also observed).
    // exchange() (not a bare store) so a Wi-Fi reconnect logs
    // MIC_DIAG_NETWORK_READY only once, not on every reassociation.
    // MIC_DIAG_GOT_IP_EVENT logs every call, though, so a reconnect storm
    // is still visible in the log.
    const uint32_t count = got_ip_event_count_.fetch_add(1, std::memory_order_relaxed) + 1;
    const bool was_ready = network_ready_.exchange(true, std::memory_order_acq_rel);
    ESP_LOGI(TAG, "MIC_DIAG_GOT_IP_EVENT count=%u network_ready_was=%d t_us=%lld", count,
             was_ready ? 1 : 0, static_cast<long long>(esp_timer_get_time()));
    if (!was_ready) {
        ESP_LOGI(TAG, "MIC_DIAG_NETWORK_READY source=got_ip t_us=%lld",
                 static_cast<long long>(esp_timer_get_time()));
    }
}

const char* MicDiagnostic::StateToString(State s) {
    switch (s) {
        case State::kWaitingForNetwork:
            return "waiting_for_network";
        case State::kWaitingForAudio:
            return "waiting_for_audio";
        case State::kWarmingUp:
            return "warming_up";
        case State::kCapturing:
            return "capturing";
        case State::kDraining:
            return "draining";
        case State::kFinished:
            return "finished";
    }
    return "unknown";
}

const char* MicDiagnostic::WifiPsTypeName(wifi_ps_type_t type) {
    switch (type) {
        case WIFI_PS_NONE:
            return "WIFI_PS_NONE";
        case WIFI_PS_MIN_MODEM:
            return "WIFI_PS_MIN_MODEM";
        case WIFI_PS_MAX_MODEM:
            return "WIFI_PS_MAX_MODEM";
        default:
            return "WIFI_PS_UNKNOWN";
    }
}

void MicDiagnostic::AdministerCaptureStart() {
    // SenderTask context only, called the first time SenderTaskLoop
    // observes state_ has reached kCapturing. By this point TapRaw has
    // already transitioned state_ (sample-accurate, on AudioInputTask) --
    // this function only handles the "administrative" follow-up, which
    // tolerates the up-to-one-poll-interval (~10ms) latency of running on
    // SenderTask instead: arming the 10s end timer, requesting the START
    // packets, and logging. None of that needs to be sample-accurate, only
    // the ring push/discard decision does (see TapRaw).
    ESP_LOGI(TAG,
             "MIC_DIAG_WARMUP_FINISHED raw_samples=%u resampled_samples=%u frames=%u "
             "raw_peak=%d raw_rms=%.1f resampled_peak=%d resampled_rms=%.1f duration_ms=%.1f "
             "t_us=%lld",
             warmup_raw_samples_seen_.load(std::memory_order_relaxed),
             warmup_resampled_samples_seen_.load(std::memory_order_relaxed),
             warmup_frames_seen_.load(std::memory_order_relaxed),
             warmup_raw_peak_.load(std::memory_order_relaxed),
             WarmupRms(warmup_raw_sum_sq_.load(std::memory_order_relaxed),
                       warmup_raw_samples_seen_.load(std::memory_order_relaxed)),
             warmup_resampled_peak_.load(std::memory_order_relaxed),
             WarmupRms(warmup_resampled_sum_sq_.load(std::memory_order_relaxed),
                       warmup_resampled_samples_seen_.load(std::memory_order_relaxed)),
             (warmup_complete_us_.load(std::memory_order_relaxed) -
              first_pcm_observed_us_.load(std::memory_order_relaxed)) /
                 1000.0,
             static_cast<long long>(warmup_complete_us_.load(std::memory_order_relaxed)));

    // Wi-Fi power-save override: this is a controlled Condition-A
    // experiment, so capture must begin from a known-good LOW_POWER
    // (WIFI_PS_MAX_MODEM) baseline and run under PERFORMANCE (WIFI_PS_NONE)
    // for its whole UDP-transmitting span. Correctness of the snapshot/
    // restore below depends entirely on the diagnostic test protocol (quiet
    // room, no wake word, no OTA/assets download in flight) ensuring no
    // other part of the firmware changes Wi-Fi power-save state during the
    // capture window -- esp_wifi_set_ps() is global, non-refcounted,
    // last-write-wins state, and closing that race for real would require
    // cross-file locking/ownership this diagnostic-only change deliberately
    // does not attempt (out of scope: application.cc/board.h/wifi_station.cc
    // are not touched here).
    //
    // Attempted at most once per run: AdministerCaptureStart() itself is
    // only ever called once (guarded by capture_window_administered in
    // SenderTaskLoop), so everything below inherits that one-shot guarantee
    // with no new synchronization needed.
    wifi_ps_type_t prev_ps = WIFI_PS_NONE;
    const esp_err_t get_err = esp_wifi_get_ps(&prev_ps);
    if (get_err != ESP_OK) {
        ESP_LOGE(TAG, "MIC_DIAG_POWERSAVE_SNAPSHOT_FAILED err=%d(%s)", get_err,
                 esp_err_to_name(get_err));
        capture_run_invalid_ = true;
        state_.store(State::kFinished, std::memory_order_release);
        return;
    }
    ESP_LOGI(TAG, "MIC_DIAG_POWERSAVE_SNAPSHOT mode=%s(%d) err=%d(%s)", WifiPsTypeName(prev_ps),
             static_cast<int>(prev_ps), get_err, esp_err_to_name(get_err));

    if (prev_ps != WIFI_PS_MAX_MODEM) {
        ESP_LOGE(TAG,
                 "MIC_DIAG_POWERSAVE_PRECONDITION_FAILED expected=%s(%d) actual=%s(%d)",
                 WifiPsTypeName(WIFI_PS_MAX_MODEM), static_cast<int>(WIFI_PS_MAX_MODEM),
                 WifiPsTypeName(prev_ps), static_cast<int>(prev_ps));
        capture_run_invalid_ = true;
        state_.store(State::kFinished, std::memory_order_release);
        return;
    }

    const esp_err_t set_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (set_err != ESP_OK) {
        ESP_LOGE(TAG, "MIC_DIAG_POWERSAVE_OVERRIDE_FAILED mode=%s(%d) err=%d(%s)",
                 WifiPsTypeName(WIFI_PS_NONE), static_cast<int>(WIFI_PS_NONE), set_err,
                 esp_err_to_name(set_err));
        // Override never took effect: nothing to restore. Do not set
        // power_save_override_active_.
        capture_run_invalid_ = true;
        state_.store(State::kFinished, std::memory_order_release);
        return;
    }
    saved_ps_type_ = prev_ps;
    power_save_override_active_ = true;
    ESP_LOGI(TAG, "MIC_DIAG_POWERSAVE_OVERRIDE mode=%s(%d) err=%d(%s)", WifiPsTypeName(WIFI_PS_NONE),
             static_cast<int>(WIFI_PS_NONE), set_err, esp_err_to_name(set_err));

    const uint32_t id = capture_id_.load();
    ESP_LOGI(TAG, "CAPTURE_STARTED id=%u raw_ring_depth=%u resampled_ring_depth=%u t_us=%lld", id,
             raw_ring_.depth(), resampled_ring_.depth(),
             static_cast<long long>(esp_timer_get_time()));

    // START packets are requested here, never sent directly: SenderTask is
    // the only task that ever calls sendto() on this socket (Fix B) --
    // see SendPendingControlPackets().
    pending_start_.store(true, std::memory_order_release);
    ESP_ERROR_CHECK(
        esp_timer_start_once(capture_end_timer_, static_cast<uint64_t>(kCaptureDurationMs) * 1000));
}

void MicDiagnostic::CaptureEndTimerCallback(void* arg) {
    static_cast<MicDiagnostic*>(arg)->FinishCapture();
}

void MicDiagnostic::FinishCapture() {
    // esp_timer task context. Non-blocking: a state flip, reading counters
    // that are no longer changing (TapRaw/TapResampled stop pushing to the
    // rings the instant state_ leaves kCapturing, checked below), and a
    // log -- no ring access beyond read-only counters, no direct sendto().
    state_.store(State::kDraining, std::memory_order_release);

    const uint32_t id = capture_id_.load();
    ESP_LOGI(TAG,
             "CAPTURE_FINISHED id=%u raw_frames_dropped=%u resampled_frames_dropped=%u "
             "raw_hwm=%u resampled_hwm=%u raw_tap_max_us=%lld resampled_tap_max_us=%lld "
             "udp_send_attempts=%u udp_send_retries=%u udp_enomem_count=%u "
             "udp_permanent_failures=%u max_packets_per_loop=%u",
             id, raw_ring_.dropped_frame_count(), resampled_ring_.dropped_frame_count(),
             raw_ring_.high_water_mark(), resampled_ring_.high_water_mark(),
             static_cast<long long>(raw_tap_max_us_.load()),
             static_cast<long long>(resampled_tap_max_us_.load()),
             udp_send_attempts_.load(std::memory_order_relaxed),
             udp_send_retries_.load(std::memory_order_relaxed),
             udp_enomem_count_.load(std::memory_order_relaxed),
             udp_permanent_failures_.load(std::memory_order_relaxed),
             max_packets_per_loop_.load(std::memory_order_relaxed));

    // END packets: same deferral as START above -- the
    // dropped_frame_count_snapshot they carry is read fresh by SenderTask
    // when it actually sends them, which is safe: state_ is already
    // kDraining by this point, so raw_ring_.dropped_frame_count() can no
    // longer change (TapRaw/TapResampled are no-ops once state_ leaves
    // kCapturing).
    pending_end_.store(true, std::memory_order_release);
}

void MicDiagnostic::TapRaw(const int16_t* samples, size_t sample_count, uint32_t source_frame_id,
                           int64_t timestamp_us) {
    const State s = state_.load(std::memory_order_relaxed);

    if (s == State::kWaitingForNetwork || s == State::kWaitingForAudio) {
        // First-ever real PCM frame observed (i.e. AudioService::
        // ReadAudioData() just read real data from the codec for the
        // first time since boot). Non-blocking: one atomic exchange plus,
        // only the first time, two relaxed stores -- no logging here (see
        // the threading-contract note in mic_diagnostic.h for why every
        // ESP_LOGx call in this class is deliberately deferred to
        // SenderTask). SenderTask polls this flag and drives the
        // kWaitingForNetwork/kWaitingForAudio -> kWarmingUp transition.
        if (!audio_input_observed_.exchange(true, std::memory_order_acq_rel)) {
            first_pcm_source_frame_id_.store(source_frame_id, std::memory_order_relaxed);
            first_pcm_observed_us_.store(timestamp_us, std::memory_order_relaxed);
        }
        return;
    }

    if (s == State::kWarmingUp) {
        // Discard this frame -- it falls inside the fixed warm-up window
        // that excludes the reproducible codec-input-enable startup
        // transient from the analyzed capture -- but still account for it
        // (frame/sample/peak/RMS counters), and use its own sample count,
        // not wall-clock time, as the warm-up clock: a stalled producer
        // must not let a wall-clock timer "expire" the warm-up early.
        warmup_frames_seen_.fetch_add(1, std::memory_order_relaxed);
        int32_t peak = 0;
        int64_t sum_sq = 0;
        AccumulatePeakSumSq(samples, sample_count, peak, sum_sq);
        // Single-writer (only TapRaw ever touches these two): plain
        // read-then-write / fetch_add, no CAS loop.
        if (peak > warmup_raw_peak_.load(std::memory_order_relaxed)) {
            warmup_raw_peak_.store(peak, std::memory_order_relaxed);
        }
        warmup_raw_sum_sq_.fetch_add(sum_sq, std::memory_order_relaxed);

        const uint32_t before =
            warmup_raw_samples_seen_.fetch_add(static_cast<uint32_t>(sample_count),
                                                std::memory_order_relaxed);
        if (before + sample_count >= kWarmupRawSampleTarget) {
            // This frame completes the warm-up target and is itself still
            // discarded (in the steady 240-samples/frame case, exactly 50
            // frames land on the 12000-sample boundary). The NEXT frame
            // (this source_frame_id + 1) is the first one either tap will
            // actually enqueue: TapRaw because it will see state_==
            // kCapturing on its next call; TapResampled because its
            // source_frame_id will then be > warmup_complete_frame_id_
            // (see TapResampled).
            State expected = State::kWarmingUp;
            if (state_.compare_exchange_strong(expected, State::kCapturing,
                                                std::memory_order_acq_rel)) {
                warmup_complete_frame_id_.store(source_frame_id, std::memory_order_relaxed);
                warmup_complete_us_.store(timestamp_us, std::memory_order_relaxed);
            }
        }
        return;
    }

    if (s != State::kCapturing) {
        return;  // kDraining / kFinished: no-op
    }

    // s == kCapturing. raw_true_sample_index_ is never touched during any
    // earlier state (see its declaration), so this fetch_add starts
    // exactly at 0 the first time this branch ever runs for this boot --
    // no explicit reset needed, and no race, since nothing else writes it.
    const uint64_t frame_first_sample =
        raw_true_sample_index_.fetch_add(sample_count, std::memory_order_relaxed);
    const int64_t t0 = esp_timer_get_time();
    raw_ring_.TryPush(source_frame_id, frame_first_sample, timestamp_us, samples, sample_count);
    const int64_t elapsed = esp_timer_get_time() - t0;
    // Single-writer (AudioInputTask): plain read-then-write, no CAS loop.
    if (elapsed > raw_tap_max_us_.load(std::memory_order_relaxed)) {
        raw_tap_max_us_.store(elapsed, std::memory_order_relaxed);
    }
}

void MicDiagnostic::TapResampled(const int16_t* samples, size_t sample_count,
                                 uint32_t source_frame_id, int64_t timestamp_us) {
    const State s = state_.load(std::memory_order_relaxed);

    if (s == State::kWaitingForNetwork || s == State::kWaitingForAudio) {
        // TapRaw (always called first for this source_frame_id, per
        // ReadAudioData()'s call order) already handles first-PCM
        // detection; nothing to do here yet.
        return;
    }

    if (s == State::kWarmingUp) {
        // Frame/sample counting for the *raw* clock is TapRaw's job only
        // (see kWarmupRawSampleTarget); this just tallies the resampled
        // side's own peak/RMS/sample-count for the warm-up report.
        int32_t peak = 0;
        int64_t sum_sq = 0;
        AccumulatePeakSumSq(samples, sample_count, peak, sum_sq);
        if (peak > warmup_resampled_peak_.load(std::memory_order_relaxed)) {
            warmup_resampled_peak_.store(peak, std::memory_order_relaxed);
        }
        warmup_resampled_sum_sq_.fetch_add(sum_sq, std::memory_order_relaxed);
        warmup_resampled_samples_seen_.fetch_add(static_cast<uint32_t>(sample_count),
                                                  std::memory_order_relaxed);
        return;
    }

    if (s != State::kCapturing) {
        return;  // kDraining / kFinished: no-op
    }

    // s == kCapturing, but this may still be the very frame whose raw
    // counterpart just crossed the warm-up threshold a few lines earlier
    // in the same ReadAudioData() call (TapRaw always runs first). Keep
    // both streams starting at the identical source_frame_id by
    // discarding that one frame here too, using the frame_id TapRaw
    // recorded at the moment of transition rather than re-deriving
    // anything from this stream's own (different) sample count.
    if (source_frame_id <= warmup_complete_frame_id_.load(std::memory_order_relaxed)) {
        int32_t peak = 0;
        int64_t sum_sq = 0;
        AccumulatePeakSumSq(samples, sample_count, peak, sum_sq);
        if (peak > warmup_resampled_peak_.load(std::memory_order_relaxed)) {
            warmup_resampled_peak_.store(peak, std::memory_order_relaxed);
        }
        warmup_resampled_sum_sq_.fetch_add(sum_sq, std::memory_order_relaxed);
        warmup_resampled_samples_seen_.fetch_add(static_cast<uint32_t>(sample_count),
                                                  std::memory_order_relaxed);
        return;
    }

    const uint64_t frame_first_sample =
        resampled_true_sample_index_.fetch_add(sample_count, std::memory_order_relaxed);
    const int64_t t0 = esp_timer_get_time();
    resampled_ring_.TryPush(source_frame_id, frame_first_sample, timestamp_us, samples,
                            sample_count);
    const int64_t elapsed = esp_timer_get_time() - t0;
    if (elapsed > resampled_tap_max_us_.load(std::memory_order_relaxed)) {
        resampled_tap_max_us_.store(elapsed, std::memory_order_relaxed);
    }
}

uint32_t MicDiagnostic::NextSourceFrameId() {
    return next_source_frame_id_.fetch_add(1, std::memory_order_relaxed);
}

MicDiagnostic::SendResult MicDiagnostic::SendPacket(const uint8_t* data, size_t length) {
    if (udp_sockfd_ < 0) {
        return SendResult::kOtherError;
    }
    udp_send_attempts_.fetch_add(1, std::memory_order_relaxed);
    ssize_t sent =
        sendto(udp_sockfd_, data, length, 0, reinterpret_cast<struct sockaddr*>(&udp_server_addr_),
               sizeof(udp_server_addr_));
    if (sent >= 0) {
        return SendResult::kSent;
    }
    if (errno == ENOMEM) {
        // Transient: lwIP couldn't allocate a pbuf/mbox entry for this
        // send right now. Counted here; the caller (DispatchSend/
        // FlushRetryQueue) decides whether to queue a retry — this
        // function never retries inline, so it can never become a tight
        // loop no matter how the caller uses it.
        udp_enomem_count_.fetch_add(1, std::memory_order_relaxed);
        return SendResult::kEnomem;
    }
    ESP_LOGW(TAG, "UDP send failed errno=%d", errno);
    return SendResult::kOtherError;
}

bool MicDiagnostic::EnqueueRetry(const uint8_t* data, size_t length, uint8_t attempts,
                                  RetryCategory category) {
    if (retry_queue_count_ >= kRetryQueueCapacity) {
        return false;
    }
    // Reserved-with-borrowing admission (Option C): sum how many slots the
    // OTHER two categories still need to reach their own reserved minimum
    // (0 if a category has already met or exceeded it, or has none, like
    // kRaw). Admit only if the capacity remaining after this admission
    // would still cover that combined need -- this is what lets an idle
    // category's reservation be freely borrowed while guaranteeing every
    // category can always eventually claim up to its own minimum,
    // regardless of arrival order or how much another category has
    // already taken from the shared (unreserved) pool.
    uint32_t other_unmet = 0;
    if (category != RetryCategory::kControl) {
        other_unmet += (retry_queue_control_count_ < kRetryReservedControl)
                           ? (kRetryReservedControl - retry_queue_control_count_)
                           : 0;
    }
    if (category != RetryCategory::kResampled) {
        other_unmet += (retry_queue_resampled_count_ < kRetryReservedResampled)
                            ? (kRetryReservedResampled - retry_queue_resampled_count_)
                            : 0;
    }
    // kRaw contributes nothing to other_unmet: kRetryReservedRaw == 0, so
    // it never has an unmet reservation to protect.
    const uint32_t free_after_admit =
        static_cast<uint32_t>(kRetryQueueCapacity - retry_queue_count_) - 1;
    if (free_after_admit < other_unmet) {
        return false;  // would eat into a slot another category still needs
    }
    PendingPacket& slot = retry_queue_[retry_queue_count_++];
    memcpy(slot.data, data, length);
    slot.length = length;
    slot.attempts = attempts;
    slot.category = category;
    switch (category) {
        case RetryCategory::kControl:
            ++retry_queue_control_count_;
            break;
        case RetryCategory::kRaw:
            ++retry_queue_raw_count_;
            break;
        case RetryCategory::kResampled:
            ++retry_queue_resampled_count_;
            break;
    }
    return true;
}

void MicDiagnostic::DecrementRetryCategoryCount(RetryCategory category) {
    switch (category) {
        case RetryCategory::kControl:
            --retry_queue_control_count_;
            break;
        case RetryCategory::kRaw:
            --retry_queue_raw_count_;
            break;
        case RetryCategory::kResampled:
            --retry_queue_resampled_count_;
            break;
    }
}

void MicDiagnostic::LogEnomemContext(const char* site, RetryCategory category, size_t length,
                                     uint8_t attempt, uint32_t budget_used, uint32_t budget_max) {
    // Diagnostic-only (temporary -- see the UDP-ENOMEM root-cause
    // investigation this exists for, and network_activity_probe.h for the
    // websocket-side half of this). Read-only: touches no retry/admission/
    // scheduling state, never calls SendPacket(), never mutates
    // retry_queue_. Safe to call from SenderTask on every ENOMEM
    // observation -- esp_wifi_sta_get_ap_info() is the only non-trivial
    // cost here, and this is off the steady-state (non-ENOMEM) hot path.
    const int64_t now_us = esp_timer_get_time();
    const uint32_t ws_count =
        NetworkActivityProbe::g_websocket_audio_send_count.load(std::memory_order_relaxed);
    const int64_t last_ws_us =
        NetworkActivityProbe::g_last_websocket_audio_send_us.load(std::memory_order_relaxed);
    // -1 sentinel ("never observed a websocket audio send this boot") is
    // unambiguous in the log, vs. a huge but real elapsed-time value.
    const int64_t ws_recency_ms = (ws_count == 0) ? -1 : (now_us - last_ws_us) / 1000;

    wifi_ap_record_t ap_info{};
    const esp_err_t rssi_err = esp_wifi_sta_get_ap_info(&ap_info);

    const char* category_str = (category == RetryCategory::kControl)   ? "control"
                                : (category == RetryCategory::kRaw)     ? "raw"
                                                                        : "resampled";
    if (rssi_err == ESP_OK) {
        ESP_LOGW(TAG,
                 "MIC_DIAG_UDP_ENOMEM_CONTEXT site=%s category=%s len=%u attempt=%u "
                 "budget_used=%u budget_max=%u ws_audio_send_count=%u ws_audio_recency_ms=%lld "
                 "rssi=%d t_us=%lld",
                 site, category_str, static_cast<unsigned>(length), attempt, budget_used,
                 budget_max, ws_count, static_cast<long long>(ws_recency_ms), ap_info.rssi,
                 static_cast<long long>(now_us));
    } else {
        ESP_LOGW(TAG,
                 "MIC_DIAG_UDP_ENOMEM_CONTEXT site=%s category=%s len=%u attempt=%u "
                 "budget_used=%u budget_max=%u ws_audio_send_count=%u ws_audio_recency_ms=%lld "
                 "rssi_err=%d(%s) t_us=%lld",
                 site, category_str, static_cast<unsigned>(length), attempt, budget_used,
                 budget_max, ws_count, static_cast<long long>(ws_recency_ms), rssi_err,
                 esp_err_to_name(rssi_err), static_cast<long long>(now_us));
    }
}

void MicDiagnostic::FlushRetryQueue(uint32_t& budget_used, uint32_t budget_max) {
    size_t write_idx = 0;
    for (size_t i = 0; i < retry_queue_count_; ++i) {
        PendingPacket& p = retry_queue_[i];
        if (budget_used >= budget_max) {
            // Out of budget this iteration: keep this (and every
            // remaining) entry queued as-is for the next iteration —
            // never dropped just for lack of pacing budget.
            retry_queue_[write_idx++] = p;
            continue;
        }
        ++budget_used;
        const SendResult result = SendPacket(p.data, p.length);
        if (result == SendResult::kSent) {
            DecrementRetryCategoryCount(p.category);
            continue;  // done: drop from the queue by not copying forward
        }
        if (result == SendResult::kOtherError) {
            // Non-transient: retrying indefinitely would itself become a
            // tight loop, so this is a permanent failure, logged once.
            udp_permanent_failures_.fetch_add(1, std::memory_order_relaxed);
            ESP_LOGW(TAG, "MIC_DIAG_UDP_RETRY_ABANDONED reason=non_enomem_error len=%u",
                     static_cast<unsigned>(p.length));
            DecrementRetryCategoryCount(p.category);
            continue;
        }
        // Still ENOMEM.
        p.attempts++;
        LogEnomemContext("retry_flush", p.category, p.length, p.attempts, budget_used, budget_max);
        if (p.attempts >= kMaxRetryAttemptsPerPacket) {
            udp_permanent_failures_.fetch_add(1, std::memory_order_relaxed);
            ESP_LOGW(TAG, "MIC_DIAG_UDP_RETRY_EXHAUSTED len=%u attempts=%u",
                     static_cast<unsigned>(p.length), p.attempts);
            DecrementRetryCategoryCount(p.category);
            continue;  // give up on this packet, don't keep it forever
        }
        udp_send_retries_.fetch_add(1, std::memory_order_relaxed);
        retry_queue_[write_idx++] = p;  // keep for the next iteration
    }
    retry_queue_count_ = write_idx;
}

void MicDiagnostic::SendPendingControlPackets(uint32_t& budget_used, uint32_t budget_max) {
    // START/END are capture-level control messages tagged kStreamControl,
    // with their own sequence-number namespace (0, 1, ... per control
    // message sent this call) — never DATA's sequence_number=0, so a
    // receiver can never mistake one for the other even by accident.
    // Only ever 2 datagrams per flag per boot (one capture), so these are
    // sent unconditionally (not budget-gated -- budget_max is forwarded to
    // DispatchSend for logging only, never checked here) rather than
    // adding complexity for a negligible pacing contribution —
    // DispatchSend still records them against budget_used for
    // max_packets_per_loop_ accuracy, so during the startup ramp (see
    // kStartupDatagramsPerIteration) these 2 packets do consume part of
    // that iteration's reduced ceiling, leaving correspondingly less for
    // the ring catch-up in DrainRawRing/DrainResampledRing -- without a
    // separate control-vs-data ordering change.
    if (pending_start_.exchange(false, std::memory_order_acq_rel)) {
        const uint32_t id = capture_id_.load();
        uint8_t packet[kHeaderLength];
        for (uint32_t ctrl_seq = 0; ctrl_seq < 2; ++ctrl_seq) {
            size_t len = SerializeHeader(packet, kStreamControl, kFlagStart, id, 0, ctrl_seq, 0, 0,
                                         esp_timer_get_time(), 0, 0);
            DispatchSend(packet, len, budget_used, budget_max, RetryCategory::kControl);
        }
    }
    if (pending_end_.exchange(false, std::memory_order_acq_rel)) {
        const uint32_t id = capture_id_.load();
        uint8_t packet[kHeaderLength];
        for (uint32_t ctrl_seq = 0; ctrl_seq < 2; ++ctrl_seq) {
            size_t len = SerializeHeader(packet, kStreamControl, kFlagEnd, id, 0, ctrl_seq, 0, 0,
                                         esp_timer_get_time(), 0, raw_ring_.dropped_frame_count());
            DispatchSend(packet, len, budget_used, budget_max, RetryCategory::kControl);
        }
    }
}

void MicDiagnostic::DispatchSend(const uint8_t* data, size_t length, uint32_t& budget_used,
                                  uint32_t budget_max, RetryCategory category) {
    ++budget_used;
    const SendResult result = SendPacket(data, length);
    if (result == SendResult::kEnomem) {
        LogEnomemContext("dispatch", category, length, /*attempt=*/1, budget_used, budget_max);
        if (EnqueueRetry(data, length, /*attempts=*/1, category)) {
            udp_send_retries_.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Retry queue admission refused this packet -- either genuinely
            // full, or admitting it would have encroached on another
            // category's reserved minimum (see EnqueueRetry()). Either way,
            // never expand the queue unboundedly: this is a real, logged
            // permanent failure, not a silent drop.
            udp_permanent_failures_.fetch_add(1, std::memory_order_relaxed);
            ESP_LOGW(TAG, "MIC_DIAG_UDP_RETRY_QUEUE_FULL len=%u category=%d",
                     static_cast<unsigned>(length), static_cast<int>(category));
        }
    } else if (result == SendResult::kOtherError) {
        udp_permanent_failures_.fetch_add(1, std::memory_order_relaxed);
    }
}

void MicDiagnostic::DrainRawRing(uint32_t& sequence_number, uint32_t& budget_used,
                                 uint32_t budget_max) {
    uint8_t packet[kMaxPacketBytes];
    RawRing::Frame frame;
    while (budget_used < budget_max && raw_ring_.TryPop(frame)) {
        const uint32_t payload_len = frame.sample_count * sizeof(int16_t);
        if (kHeaderLength + payload_len > kMaxDatagramBytes) {
            // Structurally unreachable given kRawMaxSamples's byte budget,
            // but checked explicitly rather than assumed — see item 5 of
            // the pre-build review. Counts as a drop so the receiver's
            // drop accounting still lines up.
            ESP_LOGE(TAG, "raw packet %u exceeds datagram budget (%u > %u), dropping",
                     sequence_number, static_cast<unsigned>(kHeaderLength + payload_len),
                     static_cast<unsigned>(kMaxDatagramBytes));
            continue;
        }
        // first_sample_index comes straight from the frame (set by TapRaw
        // from raw_true_sample_index_ at tap time), not accumulated here,
        // so it stays correct across any producer-side drops.
        size_t header_len = SerializeHeader(
            packet, kStreamRaw24k, kFlagData, capture_id_.load(), frame.source_frame_id,
            sequence_number, frame.first_sample_index, frame.sample_count, frame.timestamp_us,
            payload_len, raw_ring_.dropped_frame_count());
        memcpy(packet + header_len, frame.samples, payload_len);
        DispatchSend(packet, header_len + payload_len, budget_used, budget_max, RetryCategory::kRaw);
        ++sequence_number;
    }
}

void MicDiagnostic::DrainResampledRing(uint32_t& sequence_number, uint32_t& budget_used,
                                       uint32_t budget_max) {
    uint8_t packet[kMaxPacketBytes];
    ResampledRing::Frame frame;
    while (budget_used < budget_max && resampled_ring_.TryPop(frame)) {
        const uint32_t payload_len = frame.sample_count * sizeof(int16_t);
        if (kHeaderLength + payload_len > kMaxDatagramBytes) {
            ESP_LOGE(TAG, "resampled packet %u exceeds datagram budget (%u > %u), dropping",
                     sequence_number, static_cast<unsigned>(kHeaderLength + payload_len),
                     static_cast<unsigned>(kMaxDatagramBytes));
            continue;
        }
        size_t header_len = SerializeHeader(
            packet, kStreamResampled16k, kFlagData, capture_id_.load(), frame.source_frame_id,
            sequence_number, frame.first_sample_index, frame.sample_count, frame.timestamp_us,
            payload_len, resampled_ring_.dropped_frame_count());
        memcpy(packet + header_len, frame.samples, payload_len);
        DispatchSend(packet, header_len + payload_len, budget_used, budget_max,
                     RetryCategory::kResampled);
        ++sequence_number;
    }
}

void MicDiagnostic::SenderTaskEntry(void* arg) {
    static_cast<MicDiagnostic*>(arg)->SenderTaskLoop();
    vTaskDelete(nullptr);
}

void MicDiagnostic::SenderTaskLoop() {
    sender_task_started_count_.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGI(TAG, "MIC_DIAG_SENDER_TASK_STARTED t_us=%lld",
             static_cast<long long>(esp_timer_get_time()));

    uint32_t raw_seq = 0;
    uint32_t resampled_seq = 0;
    uint32_t last_capture_id = capture_id_.load();  // capture 0 already starts both seqs at 0
    int64_t last_heartbeat_us = 0;
    bool logged_first_pcm = false;
    bool capture_window_administered = false;
    // Startup ramp (temporary, diagnostic-only -- see
    // kStartupDatagramsPerIteration/kStartupRampIterations in the header
    // for the full rationale). SenderTask-only, same non-atomic
    // loop-scoped-local pattern as raw_seq/resampled_seq above -- no other
    // task reads or writes this. Armed once, at the same one-shot site
    // that calls AdministerCaptureStart() below, so it inherits that
    // call's own one-shot guarantee (see AdministerCaptureStart's doc
    // comment) rather than needing its own reset-on-new-capture-id logic
    // like raw_seq/resampled_seq have -- there is no second arming site
    // for this to reset before.
    uint32_t startup_ramp_iterations_remaining = 0;

    while (true) {
        sender_loop_count_.fetch_add(1, std::memory_order_relaxed);
        last_sender_loop_us_.store(esp_timer_get_time(), std::memory_order_relaxed);

        // Deferred socket creation: only attempt once network_ready_ has
        // been set by OnGotIp, and only once ever (udp_sockfd_ < 0 guards
        // re-entry; socket_create_failed_ stops us from retrying every 5ms
        // forever against a permanently bad config, e.g. an unparseable
        // server address). This is the only place this class ever calls
        // socket() — always well after board.StartNetwork() has run.
        if (udp_sockfd_ < 0 && !socket_create_failed_ &&
            network_ready_.load(std::memory_order_acquire)) {
            if (!CreateSocket()) {
                socket_create_failed_ = true;
            }
        }

        // MIC_DIAG_FIRST_PCM: deferred here from TapRaw (which only sets
        // the atomics, never logs -- see the threading contract) so this
        // is the first and only place this line is ever printed.
        if (!logged_first_pcm && audio_input_observed_.load(std::memory_order_acquire)) {
            logged_first_pcm = true;
            ESP_LOGI(TAG, "MIC_DIAG_FIRST_PCM t_us=%lld source_frame_id=%u",
                     static_cast<long long>(first_pcm_observed_us_.load(std::memory_order_relaxed)),
                     first_pcm_source_frame_id_.load(std::memory_order_relaxed));
        }

        // kWaitingForNetwork/kWaitingForAudio -> kWarmingUp, driven by
        // whichever of {socket ready, audio observed} resolves last --
        // either can come first (see the state-machine doc in the header).
        if (udp_sockfd_ >= 0) {
            const bool audio_ready = audio_input_observed_.load(std::memory_order_acquire);
            State cur = state_.load(std::memory_order_relaxed);
            if (cur == State::kWaitingForNetwork) {
                if (audio_ready) {
                    if (state_.compare_exchange_strong(cur, State::kWarmingUp,
                                                        std::memory_order_acq_rel)) {
                        ESP_LOGI(TAG,
                                 "MIC_DIAG_WARMUP_STARTED target_ms=%u target_raw_samples=%u t_us=%lld",
                                 kWarmupTargetMs, kWarmupRawSampleTarget,
                                 static_cast<long long>(esp_timer_get_time()));
                    }
                } else {
                    if (state_.compare_exchange_strong(cur, State::kWaitingForAudio,
                                                        std::memory_order_acq_rel)) {
                        ESP_LOGI(TAG, "MIC_DIAG_WAITING_FOR_AUDIO t_us=%lld",
                                 static_cast<long long>(esp_timer_get_time()));
                    }
                }
            } else if (cur == State::kWaitingForAudio && audio_ready) {
                if (state_.compare_exchange_strong(cur, State::kWarmingUp,
                                                    std::memory_order_acq_rel)) {
                    ESP_LOGI(TAG,
                             "MIC_DIAG_WARMUP_STARTED target_ms=%u target_raw_samples=%u t_us=%lld",
                             kWarmupTargetMs, kWarmupRawSampleTarget,
                             static_cast<long long>(esp_timer_get_time()));
                }
            }
        }

        // kWarmingUp -> kCapturing is TapRaw's own (sample-accurate)
        // transition; this is just the one-shot administrative follow-up.
        if (!capture_window_administered &&
            state_.load(std::memory_order_relaxed) == State::kCapturing) {
            capture_window_administered = true;
            AdministerCaptureStart();
            // Arm the startup ramp for the same iteration whose later
            // budget calculation (below) will consume it -- this is the
            // iteration in which SendPendingControlPackets/DrainRawRing
            // will actually send the START packets and any backlogged
            // ring frames, i.e. exactly the burst this ramp exists to
            // spread out.
            startup_ramp_iterations_remaining = kStartupRampIterations;
        }

        const uint32_t current_capture_id = capture_id_.load();
        if (current_capture_id != last_capture_id) {
            // Explicit reset on every new capture (not just "happens to be
            // zero because the task only just started") — makes the
            // per-stream sequence-number contract correct even if this
            // ever grows beyond one capture per boot.
            raw_seq = 0;
            resampled_seq = 0;
            last_capture_id = current_capture_id;
        }

        // Fix B: one shared pacing budget per iteration, spent in this
        // priority order — retries first (clear backlog before adding
        // more), then the (rare, cheap) control packets, then data. A
        // frame not drained this iteration just stays in its ring; a
        // retry not flushed this iteration just stays in the queue.
        // Neither is a new drop path.
        //
        // Startup ramp (temporary): snapshot whether the ramp is active
        // BEFORE deriving this iteration's ceiling, so the ceiling this
        // iteration's sends are actually judged against can never depend
        // on this same iteration's own countdown decrement (that
        // decrement happens further below, only after this iteration's
        // sends are done). While active, every one of the calls below
        // receives budget_max_this_iter (kStartupDatagramsPerIteration)
        // instead of kMaxDatagramsPerIteration — including
        // SendPendingControlPackets, whose 2 unconditional START/END
        // packets still increment budget_used exactly as before (they are
        // still never gated on the budget themselves), which is what lets
        // a reduced ceiling suppress this same iteration's ring catch-up
        // in DrainRawRing/DrainResampledRing without reordering anything.
        const bool startup_ramp_active_this_iter = startup_ramp_iterations_remaining > 0;
        const uint32_t budget_max_this_iter =
            startup_ramp_active_this_iter ? kStartupDatagramsPerIteration : kMaxDatagramsPerIteration;

        // Skipped entirely for an invalid run (capture_run_invalid_, set by
        // AdministerCaptureStart() on a power-save snapshot/precondition/
        // override failure): guarantees no START/DATA/END packet is ever
        // sent for that run, even for the handful of frames TapRaw may have
        // already pushed into the rings during the up-to-one-poll-interval
        // gap before AdministerCaptureStart() ran this same iteration —
        // those frames are simply abandoned in the ring, never drained.
        uint32_t budget_used = 0;
        if (!capture_run_invalid_) {
            FlushRetryQueue(budget_used, budget_max_this_iter);
            SendPendingControlPackets(budget_used, budget_max_this_iter);
            DrainRawRing(raw_seq, budget_used, budget_max_this_iter);
            DrainResampledRing(resampled_seq, budget_used, budget_max_this_iter);
        }

        // Decrement AFTER this iteration's send work is fully done (not
        // before/while deriving budget_max_this_iter above) -- keeps "was
        // the ramp active for this iteration's sends" and "count down for
        // the next iteration" clearly separated, so a future reordering of
        // the block above can never accidentally let an iteration judge
        // itself against a ceiling one count lower than it actually used.
        if (startup_ramp_active_this_iter) {
            --startup_ramp_iterations_remaining;
        }
        if (budget_used > max_packets_per_loop_.load(std::memory_order_relaxed)) {
            max_packets_per_loop_.store(budget_used, std::memory_order_relaxed);
        }

        // kDraining -> kFinished once the rings and retry queue are fully
        // flushed and END is no longer pending. Order matters: this check
        // runs AFTER the drain/flush calls above, so it reflects this
        // iteration's post-drain state. (An invalid run never reaches
        // kDraining at all -- AdministerCaptureStart() sets state_ straight
        // to kFinished -- so this block is unreachable for that path and
        // needs no capture_run_invalid_ guard of its own.)
        if (state_.load(std::memory_order_relaxed) == State::kDraining &&
            raw_ring_.depth() == 0 && resampled_ring_.depth() == 0 && retry_queue_count_ == 0 &&
            !pending_end_.load(std::memory_order_acquire)) {
            // Restore Wi-Fi power-save to its pre-capture snapshot
            // immediately before the final transition to kFinished -- i.e.
            // only after every send this class could still make (data
            // drain, retries, END) has definitely completed. Attempted at
            // most once (guarded by power_save_override_active_, which is
            // unconditionally cleared right after, whether or not the
            // restore itself succeeds) and never attempted without a
            // successful snapshot+override (power_save_override_active_ is
            // only ever set true in AdministerCaptureStart() after both
            // esp_wifi_get_ps() and esp_wifi_set_ps() succeeded) -- so this
            // never restores a guessed/default value.
            if (power_save_override_active_) {
                const esp_err_t restore_err = esp_wifi_set_ps(saved_ps_type_);
                ESP_LOGI(TAG, "MIC_DIAG_POWERSAVE_RESTORED mode=%s(%d) err=%d(%s)",
                         WifiPsTypeName(saved_ps_type_), static_cast<int>(saved_ps_type_),
                         restore_err, esp_err_to_name(restore_err));
                power_save_override_active_ = false;
            }
            state_.store(State::kFinished, std::memory_order_relaxed);
        }

        // Rate-limited heartbeat. Read-only snapshot of every counter
        // requested for root-causing why capture isn't starting, plus the
        // Fix B UDP-transport-health counters and the warm-up-in-progress
        // counters.
        const int64_t now_us = esp_timer_get_time();
        if (now_us - last_heartbeat_us >= 5 * 1000 * 1000) {
            last_heartbeat_us = now_us;
            ESP_LOGI(TAG,
                     "MIC_DIAG_HEARTBEAT loop=%u state=%s network_ready=%d audio_observed=%d "
                     "socket_fd=%d socket_attempts=%u socket_success=%u "
                     "warmup_frames=%u warmup_raw_samples=%u warmup_resampled_samples=%u "
                     "raw_depth=%u resampled_depth=%u udp_send_attempts=%u udp_send_retries=%u "
                     "udp_enomem_count=%u udp_permanent_failures=%u max_packets_per_loop=%u "
                     "retry_queue_depth=%u retry_control=%u retry_raw=%u retry_resampled=%u "
                     "t_us=%lld",
                     sender_loop_count_.load(std::memory_order_relaxed),
                     StateToString(state_.load(std::memory_order_relaxed)),
                     network_ready_.load(std::memory_order_relaxed) ? 1 : 0,
                     audio_input_observed_.load(std::memory_order_relaxed) ? 1 : 0, udp_sockfd_,
                     create_socket_attempt_count_.load(std::memory_order_relaxed),
                     create_socket_success_count_.load(std::memory_order_relaxed),
                     warmup_frames_seen_.load(std::memory_order_relaxed),
                     warmup_raw_samples_seen_.load(std::memory_order_relaxed),
                     warmup_resampled_samples_seen_.load(std::memory_order_relaxed),
                     raw_ring_.depth(), resampled_ring_.depth(),
                     udp_send_attempts_.load(std::memory_order_relaxed),
                     udp_send_retries_.load(std::memory_order_relaxed),
                     udp_enomem_count_.load(std::memory_order_relaxed),
                     udp_permanent_failures_.load(std::memory_order_relaxed),
                     max_packets_per_loop_.load(std::memory_order_relaxed),
                     static_cast<unsigned>(retry_queue_count_),
                     static_cast<unsigned>(retry_queue_control_count_),
                     static_cast<unsigned>(retry_queue_raw_count_),
                     static_cast<unsigned>(retry_queue_resampled_count_),
                     static_cast<long long>(now_us));
        }

        sender_wake_count_.fetch_add(1, std::memory_order_relaxed);
        last_sender_progress_us_.store(now_us, std::memory_order_relaxed);

        // All UDP I/O happens above, entirely off AudioInputTask. This poll
        // interval only bounds the added latency between a frame landing
        // in the ring and it going out over the network; it never affects
        // audio capture cadence. kSenderDelayTicks (not a raw
        // pdMS_TO_TICKS(5)) guarantees this is a real sleep, not vTaskDelay(0).
        vTaskDelay(kSenderDelayTicks);
    }
}

#endif  // CONFIG_MIC_DIAGNOSTIC
