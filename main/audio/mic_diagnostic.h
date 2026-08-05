#ifndef _MIC_DIAGNOSTIC_H_
#define _MIC_DIAGNOSTIC_H_

#include "sdkconfig.h"

#include <cstddef>
#include <cstdint>

#if CONFIG_MIC_DIAGNOSTIC

#include <atomic>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <esp_event.h>
#include <esp_timer.h>
#include <esp_wifi_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Temporary, passive diagnostic tap on the audio input pipeline. Not part
// of the production audio path: exists only to capture raw PCM for offline
// analysis of speech-recognition transcript errors, and is meant to be
// deleted (along with its two call sites in audio_service.cc and its
// Kconfig entries) once the investigation is done. See
// main/audio/mic_diagnostic.cc for the full design write-up.
//
// Threading contract:
//   - TapRaw()/TapResampled()/NextSourceFrameId() are called ONLY from
//     AudioService::AudioInputTask (the sole codec reader). Every branch is
//     a handful of atomic loads/stores/fetch_adds and, during the warm-up
//     window only, a tight in-register peak/sum-of-squares loop over
//     already-in-hand samples — no locks, no syscalls, no network I/O, no
//     heap allocation, no ESP_LOGx calls. Never block, never sleep. All
//     human-readable logging about state transitions this class makes is
//     deliberately deferred to SenderTask's poll loop (see below), which
//     reads the same atomics slightly later (bounded by one ~10ms poll
//     interval) — this keeps the audio-critical path 100% free of any
//     console I/O, not just "usually fast" console I/O.
//   - All UDP I/O, AND the one BSD socket() call this class ever makes,
//     happen exclusively on a separate, low-priority SenderTask, which is
//     also the only consumer of the two rings. Initialize() (called from
//     AudioService::Initialize(), i.e. before board.StartNetwork() has run
//     esp_netif_init()) therefore MUST NOT create a socket itself — lwIP's
//     tcpip task doesn't exist yet at that point, and calling socket() that
//     early hits lwIP's own "Invalid mbox" assert and reboots the board.
//     Socket creation is deferred to SenderTask, gated on network_ready_.
//   - Same reasoning applies to esp_event_handler_register(): Initialize()
//     must NOT call it either, since the DEFAULT event loop doesn't exist
//     yet at that point (esp_event_loop_create_default() only runs later,
//     inside WifiManager::Initialize(), itself called from
//     board.StartNetwork() — verified by reading both call chains). Event
//     registration is deferred to StartNetworkMonitoring(), which the
//     caller (Application::Initialize()) must invoke only after
//     board.StartNetwork() has returned.
//   - IP_EVENT_STA_GOT_IP fires on the system event task. Its handler
//     (OnGotIp) is intentionally tiny and non-blocking: it only flips
//     network_ready_ and logs — it never touches sockets. SenderTask polls
//     that flag and does the actual CreateSocket().
//   - esp_timer callbacks (capture-end only; there is no more arm timer —
//     see the state-machine note below) run on the system timer task; they
//     only flip atomics/state that TapRaw/TapResampled/SenderTask read.
//
// State machine (replaces the old fixed-3000ms-after-GOT_IP arm timer,
// which raced the production audio pipeline's own, independently-timed
// startup — see the investigation that led to this design: GOT_IP has no
// causal relationship to when AudioCodec::EnableInput(true) actually
// fires, since that's gated by OTA-check + MQTT-connect + StateMachine
// reaching "idle", not by Wi-Fi at all):
//
//   kWaitingForNetwork -> kWaitingForAudio -> kWarmingUp -> kCapturing
//       -> kDraining -> kFinished
//
//   - kWaitingForNetwork: initial state. SenderTask is polling for the
//     socket to become creatable (network_ready_) and TapRaw/TapResampled
//     are watching for the first real PCM frame (audio_input_observed_),
//     in either order — whichever resolves first, SenderTask moves the
//     state machine to kWaitingForAudio (network ready first) or straight
//     to kWarmingUp (audio observed first, network resolves the same poll
//     cycle it goes ready).
//   - kWaitingForAudio: socket exists; still waiting on the very first
//     TapRaw() call with real data (i.e. the first time
//     AudioService::ReadAudioData() successfully reads from the codec
//     after calling EnableInput(true) for the first time). Set from
//     TapRaw's own observation, consumed/transitioned by SenderTask.
//   - kWarmingUp: real audio is flowing, but every frame is discarded
//     (never pushed to a ring, never sent) until kWarmupRawSampleTarget
//     raw (24kHz) samples have been seen. This directly excludes the
//     reproducible ES8311 input-enable startup transient (confirmed via
//     waveform analysis: sharp onset, monotonic decay, converges to
//     background noise level by ~450-500ms in two independent captures)
//     from the analysis window, without touching gain/AGC/NS/AEC. Raw
//     sample count is the canonical warm-up clock (see TapResampled's
//     doc comment for why the resampled stream never makes its own
//     warm-up decision).
//   - kCapturing: the real, analyzed 10-second window. Started the instant
//     TapRaw's own sample counter crosses the warm-up target — sample-
//     accurate, not poll-latency-bound like the old timer-based arm.
//   - kDraining: the 10s capture_end_timer_ has fired; TapRaw/TapResampled
//     stopped accepting new frames, but SenderTask may still be draining
//     already-queued ring contents and/or retrying a queued END packet.
//   - kFinished: rings empty, retry queue empty, END no longer pending.
class MicDiagnostic {
public:
    static MicDiagnostic& GetInstance();

    // Call once during board bring-up, BEFORE board.StartNetwork() —
    // allocates both PSRAM rings, creates the capture-end timer, and
    // starts the sender task. Deliberately does NOT touch the network in
    // any way (no socket, no event registration): both are created later,
    // only once network_ready_ is set (see StartNetworkMonitoring()/
    // CreateSocket()). Logs MIC_DIAG_INIT on success or MIC_DIAG_DISABLED
    // reason=<...> on failure (allocation/task/timer); on failure every
    // subsequent Tap* call is a cheap no-op for the rest of this boot and
    // production audio is completely unaffected.
    void Initialize();

    // Call once, AFTER board.StartNetwork() has returned (i.e. after the
    // default event loop is guaranteed to exist — see the threading
    // contract above). Registers the IP_EVENT_STA_GOT_IP handler; logs the
    // registration's return code either way (MIC_DIAG_NETWORK_MONITOR_
    // REGISTERED on success, MIC_DIAG_DISABLED reason=... err=<code> on
    // failure) and never aborts. Idempotent: a second call is a no-op.
    // Also covers the race where Wi-Fi already had an IP before this was
    // called (events aren't redelivered retroactively) by checking
    // WifiManager::GetInstance().IsConnected() right after a successful
    // registration and logging MIC_DIAG_NETWORK_ALREADY_READY if so.
    void StartNetworkMonitoring();

    // Producer-side taps, called from AudioInputTask only. Behavior
    // depends on the current state (see the state-machine doc above):
    // no-op while waiting for network/audio; discarded-but-counted while
    // warming up; pushed to the ring while capturing; no-op once
    // draining/finished. Always a handful of atomic ops, at most a tight
    // in-register loop over already-in-hand samples — see the threading
    // contract above for the full non-blocking guarantee.
    void TapRaw(const int16_t* samples, size_t sample_count, uint32_t source_frame_id,
                int64_t timestamp_us);
    // Never makes its own warm-up/capture decision from its own sample
    // count: it strictly follows state_ (and, for the one frame where
    // TapRaw's raw-sample counter crosses the warm-up target, the stored
    // warmup_complete_frame_id_) so that raw and resampled always start
    // capturing at the identical source_frame_id, even though a single
    // ReadAudioData() call produces a different sample count for each
    // stream (240 vs 160 samples) — see mic_diagnostic.cc for the full
    // reasoning.
    void TapResampled(const int16_t* samples, size_t sample_count, uint32_t source_frame_id,
                      int64_t timestamp_us);

    // Returns a monotonically increasing id, one per ReadAudioData() call.
    // Call once per invocation and pass the same value to both TapRaw and
    // TapResampled for that invocation, so a receiver can prove a given
    // 24kHz frame and 16kHz frame came from the same input block.
    uint32_t NextSourceFrameId();

private:
    enum class State : uint8_t {
        kWaitingForNetwork,
        kWaitingForAudio,
        kWarmingUp,
        kCapturing,
        kDraining,
        kFinished,
    };

    template <size_t kMaxSamples, size_t kCapacity>
    class FrameRing {
    public:
        struct Frame {
            uint32_t source_frame_id;
            uint64_t first_sample_index;  // true position in the continuous stream: assigned by
                                          // the caller from a counter that advances on every tap
                                          // attempt, including ones this ring later drops, so a
                                          // gap here always reflects a real time gap in the audio
            int64_t timestamp_us;
            uint16_t sample_count;
            int16_t samples[kMaxSamples];
        };

        bool Init();  // allocates slots_ from PSRAM; false on failure

        // Producer only (AudioInputTask). All-or-nothing: never writes a
        // partial frame. Returns false and counts a dropped FRAME (not a
        // dropped sample count) if full or if sample_count exceeds
        // kMaxSamples.
        bool TryPush(uint32_t source_frame_id, uint64_t first_sample_index, int64_t timestamp_us,
                     const int16_t* samples, size_t sample_count);

        // Consumer only (SenderTask).
        bool TryPop(Frame& out);

        // Number of frames (not samples) this ring has dropped since the
        // last ResetCounters().
        uint32_t dropped_frame_count() const {
            return dropped_frame_count_.load(std::memory_order_relaxed);
        }
        uint32_t high_water_mark() const {
            return high_water_mark_.load(std::memory_order_relaxed);
        }
        void ResetCounters();

        // Diagnostics only: current occupancy, approximate — head_/tail_
        // are read with no synchronization between the two loads, so this
        // can be momentarily stale, which is fine for a heartbeat log and
        // not used for any correctness-critical logic.
        uint32_t depth() const {
            return head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed);
        }

    private:
        Frame* slots_ = nullptr;
        std::atomic<uint32_t> head_{0};
        std::atomic<uint32_t> tail_{0};
        std::atomic<uint32_t> dropped_frame_count_{0};
        // Still atomic (FinishCapture(), running on the esp_timer task,
        // reads this cross-thread), but TryPush is its only writer, so the
        // update is a plain relaxed load + conditional store — no CAS/retry
        // loop, because there is no concurrent writer to race against.
        std::atomic<uint32_t> high_water_mark_{0};
    };

    static constexpr size_t kRawMaxSamples = 256;        // >= 240 (24kHz @ 10ms)
    static constexpr size_t kResampledMaxSamples = 192;  // >= 160 (16kHz @ 10ms)
    // Deliberately different from kRawMaxSamples: matches each stream's
    // real per-frame sample count more closely (memory budget), and keeps
    // RawRing/ResampledRing as genuinely distinct FrameRing<> types.
    static constexpr size_t kRingCapacityFrames = 50;  // ~500ms of headroom at 10ms/frame
    static constexpr uint32_t kCaptureDurationMs = 10000;
    // Warm-up window: excludes the reproducible codec-input-enable startup
    // transient from the analyzed capture. kWarmupTargetMs is descriptive
    // only (used in logs); the actual gate is kWarmupRawSampleTarget, a
    // sample count, so it tracks real audio time even if the producer
    // stalls or the tick rate changes — not wall-clock time (per the
    // explicit requirement: a wall-clock timer would keep "expiring" even
    // if frames stop flowing, which a sample-count gate structurally
    // cannot do). 24000 Hz * 0.5s = 12000 samples exactly.
    static constexpr uint32_t kWarmupTargetMs = 500;
    static constexpr uint32_t kWarmupRawSampleTarget = 12000;
    static constexpr uint16_t kStreamRaw24k = 0;
    static constexpr uint16_t kStreamResampled16k = 1;
    // Wire-format sizing, duplicated from the anonymous-namespace constants
    // in mic_diagnostic.cc (kHeaderLength=50, kMaxPayloadBytes=
    // kRawMaxSamples*sizeof(int16_t)) only because PendingPacket below
    // needs a compile-time buffer size and those constants live in free
    // functions outside this class. Keep in sync if the wire header layout
    // or kRawMaxSamples ever changes.
    static constexpr size_t kMaxPacketBytes = 50 + kRawMaxSamples * sizeof(int16_t);
    // START/END are capture-level control messages, not stream data, and
    // use their own sequence-number namespace (see StartCapture/
    // FinishCapture) so they can never collide with — or be confused for
    // — a DATA packet's sequence_number=0.
    static constexpr uint16_t kStreamControl = 2;

    // Bounded ENOMEM retry (Fix B): a fixed-capacity, SenderTask-owned
    // (single-task, no synchronization needed) queue of datagrams that hit
    // a transient sendto() ENOMEM and are retried on a later loop
    // iteration instead of being silently dropped. kMaxRetryAttemptsPerPacket
    // bounds how long a single packet is retried before being counted as a
    // permanent failure; kRetryQueueCapacity bounds total memory, so a
    // sustained (non-transient) failure condition can't grow unbounded.
    static constexpr size_t kRetryQueueCapacity = 16;
    static constexpr uint8_t kMaxRetryAttemptsPerPacket = 5;
    // Pacing (Fix B): caps how many datagrams SenderTaskLoop sends in a
    // single iteration (retries + control + drained data, combined).
    // Steady state is ~2 datagrams/iteration (one raw + one resampled
    // frame) at the ~10ms sender cadence, so this gives headroom while
    // still bounding a burst. Frames not sent this iteration are simply
    // not popped from their ring (see DrainRawRing/DrainResampledRing) --
    // no new drop path, they're drained on a later iteration.
    static constexpr uint32_t kMaxDatagramsPerIteration = 8;

    // Startup ramp (temporary, diagnostic-only -- see the UDP-ENOMEM
    // root-cause investigation this exists for): for the first
    // kStartupRampIterations SenderTaskLoop iterations after
    // AdministerCaptureStart() runs, the per-iteration datagram budget is
    // capped at kStartupDatagramsPerIteration instead of
    // kMaxDatagramsPerIteration. Root cause: CAPTURE_STARTED's own
    // administrative follow-up (2 unconditional START packets) lands in
    // the same iteration as the first ring catch-up drain, and that
    // compounds with retry-queue churn across several following
    // iterations -- confirmed on real hardware (Condition-A capture) as a
    // ~171ms/~17-iteration ENOMEM burst immediately after CAPTURE_STARTED,
    // with zero ENOMEM for the remaining ~9.8s of the capture. A lower
    // ceiling here spreads that same startup work across more of the
    // loop's own already-existing ~10ms cadence (kSenderDelayTicks) --
    // no new delay/blocking primitive is introduced, see SenderTaskLoop().
    // kStartupRampIterations (~300ms) is chosen with margin over the
    // observed burst duration (~171ms typical, ~293ms worst case across 5
    // validated runs), and is deliberately NOT shortened when tuning the
    // budget below -- snapping the ceiling back mid-congestion would risk
    // a second burst at the ramp boundary.
    //
    // kStartupDatagramsPerIteration was originally 3, which measurably
    // over-throttled: SenderTaskLoop spends this one shared budget on
    // retries FIRST (FlushRetryQueue), then control, then both rings, and
    // steady state already needs ~2 datagrams/iteration (1 raw + 1
    // resampled). At 3 that left a single spare slot, so once the retry
    // queue held more than one entry it could consume the whole iteration
    // and starve DrainRawRing/DrainResampledRing outright -- observed on
    // real hardware as resampled_hwm reaching the ring's full 50-frame
    // capacity and dropping 2 frames (1 of 5 runs), with hwm at 41-43 in
    // two more. 5 keeps meaningful suppression of the capture-start burst
    // (still well under kMaxDatagramsPerIteration=8, whose unramped
    // behavior never dropped a frame) while restoring ~3 spare slots over
    // steady-state demand so retry-flush and ring-drain can coexist.
    static constexpr uint32_t kStartupDatagramsPerIteration = 5;
    static constexpr uint32_t kStartupRampIterations = 30;

    // Retry-queue admission categories (Option C: reserved-with-borrowing
    // fairness). All three still share the one retry_queue_ array/capacity
    // above -- these constants only govern EnqueueRetry()'s admission
    // decision, never a separate allocation. kControl/kResampled each get a
    // guaranteed minimum; kRaw has none and only ever draws from whatever
    // is left unreserved (kRetryQueueCapacity - kRetryReservedControl -
    // kRetryReservedResampled = 8 slots). Reservations are borrowable: an
    // idle category's minimum is free for others to use until that
    // category actually needs it, at which point it can always reclaim up
    // to its own minimum -- see EnqueueRetry()'s doc comment for the exact
    // formula. Root cause and rationale: raw is always drained before
    // resampled in SenderTaskLoop(), so under transient sendto() ENOMEM
    // near CAPTURE_STARTED, raw could previously fill the single shared
    // queue before resampled's earliest packets got a single retry
    // attempt, producing a clean leading-edge loss on resampled while raw
    // merely retried-then-exhausted mid-stream. This reservation prevents
    // that starvation without splitting the queue.
    enum class RetryCategory : uint8_t { kControl, kRaw, kResampled };
    static constexpr uint32_t kRetryReservedControl = 2;
    static constexpr uint32_t kRetryReservedResampled = 6;
    static constexpr uint32_t kRetryReservedRaw = 0;

    struct PendingPacket {
        uint8_t data[kMaxPacketBytes];
        size_t length = 0;
        uint8_t attempts = 0;
        RetryCategory category = RetryCategory::kControl;
    };

    enum class SendResult : uint8_t { kSent, kEnomem, kOtherError };

    using RawRing = FrameRing<kRawMaxSamples, kRingCapacityFrames>;
    using ResampledRing = FrameRing<kResampledMaxSamples, kRingCapacityFrames>;

    MicDiagnostic() = default;

    static void GotIpHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data);
    static void CaptureEndTimerCallback(void* arg);
    static void SenderTaskEntry(void* arg);

    // System-event-task context. Non-blocking: only sets network_ready_ and
    // logs. Never creates a socket, never sends anything — see the
    // threading contract above.
    void OnGotIp();
    // esp_timer task context (fired kCaptureDurationMs after kCapturing
    // began). Non-blocking: flips state_ to kDraining, logs
    // CAPTURE_FINISHED from the counters already gathered, and sets
    // pending_end_ — never touches the socket directly (SenderTask sends
    // the actual END packets; see SendPendingControlPackets).
    void FinishCapture();
    void SenderTaskLoop();
    // SenderTask context only. Idempotent: a no-op success if udp_sockfd_
    // is already valid, so a Wi-Fi reconnect (a second GOT_IP) can never
    // create a second socket without closing the first. Parses and
    // validates the configured server IP/port; on any failure, logs
    // MIC_DIAG_DISABLED and returns false — never asserts/aborts.
    bool CreateSocket();
    // SenderTask context only, called the first time SenderTaskLoop
    // observes state_ has reached kCapturing (set by TapRaw the instant
    // the warm-up sample target is crossed). Logs MIC_DIAG_WARMUP_FINISHED
    // and CAPTURE_STARTED from the counters TapRaw/TapResampled already
    // gathered, arms capture_end_timer_, and requests the START packets
    // via pending_start_ — never touches the socket directly.
    void AdministerCaptureStart();
    // One raw sendto() attempt (SenderTask-only). Counts the attempt and,
    // on ENOMEM, counts it -- but never retries inline: the caller decides
    // whether/how to retry (see DispatchSend/FlushRetryQueue below). This
    // split is what keeps a transient ENOMEM from ever becoming a tight
    // retry loop.
    SendResult SendPacket(const uint8_t* data, size_t length);
    // Copies data into the retry queue (SenderTask-only, no locks needed:
    // single owner). Category-aware admission (Option C): returns false if
    // the queue is at kRetryQueueCapacity, OR if admitting this packet
    // would drop free capacity below what the OTHER two categories still
    // need to reach their own reserved minimum (kRetryReservedControl/
    // kRetryReservedResampled) -- i.e. a category may freely borrow
    // another's unused reservation, but can never be blocked by its own
    // shortfall, only by encroaching on someone else's. Caller must count
    // a false return as a permanent failure, never retry harder to force
    // it in.
    bool EnqueueRetry(const uint8_t* data, size_t length, uint8_t attempts,
                       RetryCategory category);
    // Attempts to resend every queued packet, bounded by the shared
    // per-iteration datagram budget. A packet that succeeds or that
    // exceeds kMaxRetryAttemptsPerPacket is removed from the queue (the
    // latter counted in udp_permanent_failures_, always logged -- never a
    // silent drop of a transient failure). Every removal decrements the
    // matching per-category live count via DecrementRetryCategoryCount().
    void FlushRetryQueue(uint32_t& budget_used, uint32_t budget_max);
    // Single place that keeps retry_queue_{control,raw,resampled}_count_
    // in sync with an entry actually leaving retry_queue_ (success,
    // non-ENOMEM permanent failure, or attempts-exhausted) -- kept as one
    // helper rather than three inline decrements so every removal path in
    // FlushRetryQueue() updates the same category the same way.
    void DecrementRetryCategoryCount(RetryCategory category);
    // Sends the 2 START or 2 END control packets if AdministerCaptureStart()/
    // FinishCapture() requested one via pending_start_/pending_end_. This
    // -- so SenderTask is the ONLY task that ever calls sendto() on this
    // socket -- is Fix B's core change: any other task calling sendto()
    // directly used to race SenderTask's own ring drain, which was the
    // confirmed cause of a 15-packet ENOMEM burst at the old capture-start
    // instant. Still sent unconditionally regardless of budget_max (never
    // gated, never skipped/deferred) -- the startup ramp (see
    // kStartupDatagramsPerIteration) does not change that. budget_max is
    // accepted only to forward into DispatchSend() so its ENOMEM logging
    // reports the real active ceiling; it never gates control-packet
    // sending itself.
    void SendPendingControlPackets(uint32_t& budget_used, uint32_t budget_max);
    // Shared helper for every call site that has a fully-serialized
    // packet ready to send (control packets, retry-queue flush, and
    // freshly-drained ring frames): spends one unit of the per-iteration
    // budget, attempts the send, and on ENOMEM hands it to the retry
    // queue rather than dropping it. category identifies which retry-queue
    // admission class this packet belongs to (Option C) -- forwarded
    // straight to EnqueueRetry() on ENOMEM. budget_max is never used to
    // gate anything here (the gating already happens in each caller's own
    // while/if condition before DispatchSend is even called) -- it is
    // forwarded purely so LogEnomemContext() reports the real active
    // per-iteration ceiling (kStartupDatagramsPerIteration during the
    // startup ramp, kMaxDatagramsPerIteration otherwise) instead of a
    // stale constant.
    void DispatchSend(const uint8_t* data, size_t length, uint32_t& budget_used,
                       uint32_t budget_max, RetryCategory category);
    // Diagnostic-only (temporary, see the UDP-ENOMEM root-cause
    // investigation this exists for): logs one MIC_DIAG_UDP_ENOMEM_CONTEXT
    // line per ENOMEM observation, correlating it with production
    // WebsocketProtocol audio-send recency (see network_activity_probe.h),
    // this iteration's shared send budget position, and current RSSI --
    // purely additive observability. Called from DispatchSend's and
    // FlushRetryQueue's existing kEnomem branches; never influences any
    // retry/admission/scheduling decision, and never itself calls
    // SendPacket() or touches retry_queue_.
    void LogEnomemContext(const char* site, RetryCategory category, size_t length, uint8_t attempt,
                          uint32_t budget_used, uint32_t budget_max);
    // Diagnostics only: human-readable state name for the
    // MIC_DIAG_HEARTBEAT log line.
    static const char* StateToString(State s);
    // Diagnostics only: human-readable Wi-Fi power-save mode name for the
    // MIC_DIAG_POWERSAVE_* log lines. No heap allocation — returns a string
    // literal.
    static const char* WifiPsTypeName(wifi_ps_type_t type);
    // Two distinct methods (not an overload pair) so a future change to
    // kRawMaxSamples/kResampledMaxSamples can never silently collide two
    // identical FrameRing<> instantiations into one ambiguous overload.
    // sequence_number is reset to 0 by the caller (SenderTaskLoop) at the
    // start of each new capture_id; first_sample_index is read straight
    // out of each popped Frame (see FrameRing::Frame), not accumulated
    // here, so it stays correct across producer-side drops. budget_used/
    // budget_max (Fix B) bound how many frames get popped+sent in a
    // single call -- a frame not popped this call simply stays in the
    // ring, so this never adds a new drop path.
    void DrainRawRing(uint32_t& sequence_number, uint32_t& budget_used, uint32_t budget_max);
    void DrainResampledRing(uint32_t& sequence_number, uint32_t& budget_used, uint32_t budget_max);

    RawRing raw_ring_;
    ResampledRing resampled_ring_;

    std::atomic<State> state_{State::kWaitingForNetwork};
    // Set once (non-blocking) by OnGotIp on the system event task; polled
    // by SenderTask, which is the only thing that ever acts on it. A later
    // Wi-Fi reconnect just re-stores true — harmless, since CreateSocket()
    // is idempotent past the first time.
    std::atomic<bool> network_ready_{false};
    // Guards StartNetworkMonitoring() so a second call (there's no reason
    // for one today, but this makes it safe regardless) never registers
    // the event handler twice.
    std::atomic<bool> network_monitor_started_{false};

    // Set once (non-blocking exchange) by TapRaw the first time it is
    // called with state_ still kWaitingForNetwork/kWaitingForAudio, i.e.
    // the first time AudioService::ReadAudioData() successfully reads real
    // PCM from the codec after calling EnableInput(true) for the first
    // time. This — not GOT_IP — is the real "audio input ready" signal;
    // SenderTask polls it to drive kWaitingForNetwork/kWaitingForAudio ->
    // kWarmingUp.
    std::atomic<bool> audio_input_observed_{false};
    std::atomic<uint32_t> first_pcm_source_frame_id_{0};
    std::atomic<int64_t> first_pcm_observed_us_{0};

    // Warm-up bookkeeping (all single-writer from TapRaw except
    // warmup_resampled_*, which TapResampled writes — no two writers ever
    // touch the same counter). warmup_raw_samples_seen_ is the actual gate
    // (see TapRaw); the rest exist purely for the required warm-up report
    // (frame/sample counts, peak, RMS) and are read cross-thread by
    // SenderTask once, in AdministerCaptureStart(), well after they stop
    // changing (TapRaw has already moved state_ off kWarmingUp by then).
    std::atomic<uint32_t> warmup_frames_seen_{0};
    std::atomic<uint32_t> warmup_raw_samples_seen_{0};
    std::atomic<uint32_t> warmup_resampled_samples_seen_{0};
    std::atomic<int32_t> warmup_raw_peak_{0};
    std::atomic<int64_t> warmup_raw_sum_sq_{0};
    std::atomic<int32_t> warmup_resampled_peak_{0};
    std::atomic<int64_t> warmup_resampled_sum_sq_{0};
    // The source_frame_id of the one frame whose raw sample count crossed
    // kWarmupRawSampleTarget (TapRaw discards this frame too, then flips
    // state_ to kCapturing). TapResampled compares its own source_frame_id
    // against this so the resampled tap for that SAME frame — which runs
    // moments after TapRaw's, inside the same ReadAudioData() call, and
    // would otherwise already observe state_==kCapturing — still discards
    // it, keeping both streams' first captured frame at the same
    // source_frame_id despite raw/resampled having different per-frame
    // sample counts (240 vs 160).
    std::atomic<uint32_t> warmup_complete_frame_id_{0};
    std::atomic<int64_t> warmup_complete_us_{0};

    // Diagnostics only: read-only observability into whether SenderTask is
    // being scheduled at all, and how far it's getting each time it is.
    // All single-writer (only SenderTaskLoop/CreateSocket/OnGotIp ever
    // touch their own counter), plain relaxed atomics — no CAS loops,
    // nothing here changes scheduling behavior.
    std::atomic<uint32_t> sender_task_started_count_{0};
    std::atomic<uint32_t> sender_loop_count_{0};
    std::atomic<uint32_t> sender_wake_count_{0};
    std::atomic<uint32_t> create_socket_attempt_count_{0};
    std::atomic<uint32_t> create_socket_success_count_{0};
    std::atomic<uint32_t> got_ip_event_count_{0};
    // Fix B (UDP transport hardening) counters/state -- same single-writer
    // (SenderTask only), plain-relaxed-atomic pattern as the rest of this
    // block; nothing here changes scheduling behavior, only observability.
    std::atomic<uint32_t> udp_send_attempts_{0};
    std::atomic<uint32_t> udp_send_retries_{0};
    std::atomic<uint32_t> udp_enomem_count_{0};
    std::atomic<uint32_t> udp_permanent_failures_{0};
    std::atomic<uint32_t> max_packets_per_loop_{0};
    // Set (non-blocking) by AdministerCaptureStart()/FinishCapture();
    // consumed (exchanged back to false) by SenderTask in
    // SendPendingControlPackets(). Same deferred-work pattern already used
    // for network_ready_/OnGotIp().
    std::atomic<bool> pending_start_{false};
    std::atomic<bool> pending_end_{false};
    // Wall-clock (esp_timer) markers: last_sender_loop_us updates at the
    // TOP of every loop iteration; last_sender_progress_us updates only
    // once an iteration reaches its own vTaskDelay. If loop_us keeps
    // advancing but progress_us stalls, SenderTask is getting scheduled
    // but stuck somewhere mid-iteration (CreateSocket/DrainRing/etc.);
    // if neither ever advances past 0, SenderTask never ran at all.
    std::atomic<int64_t> last_sender_loop_us_{0};
    std::atomic<int64_t> last_sender_progress_us_{0};

    std::atomic<uint32_t> next_source_frame_id_{0};
    std::atomic<uint32_t> capture_id_{0};
    std::atomic<int64_t> raw_tap_max_us_{0};
    std::atomic<int64_t> resampled_tap_max_us_{0};
    // True sample position in the continuous CAPTURED stream: advanced by
    // sample_count on every tap ATTEMPT while state_==kCapturing (see
    // TapRaw/TapResampled), whether or not the frame ends up dropped by
    // the ring. This is what makes a receiver-visible gap in
    // first_sample_index mean something real ("N samples of audio
    // happened here that we don't have"), independent of whether the loss
    // was a producer-side drop or a lost UDP packet. Single-writer
    // (AudioInputTask via TapRaw/TapResampled). Deliberately never touched
    // during kWaitingForNetwork/kWaitingForAudio/kWarmingUp, so it starts
    // at exactly 0 the first time kCapturing's branch runs — no explicit
    // reset needed, and no race with any other writer, since nothing else
    // ever writes these two fields.
    std::atomic<uint64_t> raw_true_sample_index_{0};
    std::atomic<uint64_t> resampled_true_sample_index_{0};

    // SenderTask-only (not atomic: SenderTask is the sole reader/writer of
    // both, and udp_sockfd_'s validity is what gates re-attempting
    // CreateSocket() in the first place — see SenderTaskLoop()).
    int udp_sockfd_ = -1;
    bool socket_create_failed_ = false;
    // Wi-Fi power-save override (SenderTask-only, same non-atomic
    // single-owner category as udp_sockfd_/socket_create_failed_ above).
    // MicDiagnostic is the ONLY caller of esp_wifi_get_ps()/esp_wifi_set_ps()
    // in this file; correctness of the snapshot/restore depends entirely on
    // the diagnostic test protocol ensuring no other part of the firmware
    // (voice session, OTA, assets download) changes Wi-Fi power-save state
    // during the capture window -- see the design note in
    // AdministerCaptureStart() for why this can't be closed with a lock or
    // refcount confined to this file. saved_ps_type_ is only meaningful
    // when power_save_override_active_ is true.
    wifi_ps_type_t saved_ps_type_ = WIFI_PS_NONE;
    bool power_save_override_active_ = false;
    // Set true (once) if the power-save snapshot/precondition/override
    // sequence fails inside AdministerCaptureStart(). Gates
    // SendPendingControlPackets()/DrainRawRing()/DrainResampledRing() so an
    // invalid run sends no START/DATA/END packets, even for the handful of
    // frames TapRaw may have already pushed into the rings before
    // AdministerCaptureStart() got to run (see the state-machine doc for
    // why that race window exists and is normally harmless).
    bool capture_run_invalid_ = false;
    // SenderTask-only (see PendingPacket/kRetryQueueCapacity above): plain
    // array + count, no atomics, since SenderTaskLoop is the sole
    // reader/writer of both.
    PendingPacket retry_queue_[kRetryQueueCapacity];
    size_t retry_queue_count_ = 0;
    // Per-category live counts (Option C), kept in lockstep with entries
    // actually present in retry_queue_ -- incremented in EnqueueRetry() on
    // admission, decremented in FlushRetryQueue() via
    // DecrementRetryCategoryCount() on every removal. Their sum always
    // equals retry_queue_count_; SenderTask-only, same non-atomic
    // single-owner category as retry_queue_count_ above.
    uint32_t retry_queue_control_count_ = 0;
    uint32_t retry_queue_raw_count_ = 0;
    uint32_t retry_queue_resampled_count_ = 0;
    struct sockaddr_in udp_server_addr_{};
    TaskHandle_t sender_task_handle_ = nullptr;
    esp_timer_handle_t capture_end_timer_ = nullptr;
    bool initialized_ = false;
};

#else  // !CONFIG_MIC_DIAGNOSTIC

// Zero-footprint stand-in so the call sites in audio_service.cc and
// application.cc need no #if guard: everything below compiles away to
// nothing when the feature is off.
class MicDiagnostic {
public:
    static MicDiagnostic& GetInstance() {
        static MicDiagnostic instance;
        return instance;
    }
    void Initialize() {}
    void StartNetworkMonitoring() {}
    void TapRaw(const int16_t*, size_t, uint32_t, int64_t) {}
    void TapResampled(const int16_t*, size_t, uint32_t, int64_t) {}
    uint32_t NextSourceFrameId() { return 0; }
};

#endif  // CONFIG_MIC_DIAGNOSTIC

#endif  // _MIC_DIAGNOSTIC_H_
