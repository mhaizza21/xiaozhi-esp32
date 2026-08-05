#ifndef _NETWORK_ACTIVITY_PROBE_H_
#define _NETWORK_ACTIVITY_PROBE_H_

#include "sdkconfig.h"

#if CONFIG_MIC_DIAGNOSTIC

#include <atomic>
#include <cstdint>

#include <esp_timer.h>

// Temporary, diagnostic-only cross-module probe (see mic_diagnostic.h for
// the broader temporary-diagnostic contract this belongs to -- meant to be
// deleted alongside it once the mic-capture / UDP-ENOMEM investigation is
// done). Lets MicDiagnostic's UDP ENOMEM logging report whether production
// audio traffic (WebsocketProtocol::SendAudio) was active on the radio at
// the same moment, to help distinguish shared-TX-buffer contention with
// the production audio stream from other causes of UDP send pressure.
//
// Two relaxed atomics, written from WebsocketProtocol::SendAudio() (main
// Application task) and read from MicDiagnostic's SenderTask -- no lock
// needed: both sides already treat this as a best-effort diagnostic
// snapshot, not a synchronization primitive, and neither side's own
// behavior depends on the other's value.
namespace NetworkActivityProbe {

inline std::atomic<uint32_t> g_websocket_audio_send_count{0};
inline std::atomic<int64_t> g_last_websocket_audio_send_us{0};

// Called once per WebsocketProtocol::SendAudio() attempt (success or
// failure -- we care whether the radio was asked to carry production audio
// traffic, not whether that particular send succeeded). Non-blocking: one
// fetch_add plus one store, no I/O, no allocation.
inline void NoteWebsocketAudioSend() {
    g_websocket_audio_send_count.fetch_add(1, std::memory_order_relaxed);
    g_last_websocket_audio_send_us.store(esp_timer_get_time(), std::memory_order_relaxed);
}

}  // namespace NetworkActivityProbe

#else  // !CONFIG_MIC_DIAGNOSTIC

// Zero-footprint stand-in so call sites (websocket_protocol.cc) need no
// #if guard, matching the pattern in mic_diagnostic.h.
namespace NetworkActivityProbe {
inline void NoteWebsocketAudioSend() {}
}  // namespace NetworkActivityProbe

#endif  // CONFIG_MIC_DIAGNOSTIC

#endif  // _NETWORK_ACTIVITY_PROBE_H_
