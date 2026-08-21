#ifndef WAKE_WORD_COOLDOWN_H
#define WAKE_WORD_COOLDOWN_H

#include <cstdint>

// Decision for a wake-word trigger arriving `now_ms` after the audio channel
// last closed at `closed_at_ms`. kSuppressAndRearm means the trigger is
// treated as a likely self-trigger echo of the device's own TTS output (on
// boards without echo cancellation) and the caller must re-arm the detector,
// since it stops itself on firing.
enum class WakeWordCooldownDecision {
    kProceed,
    kSuppressAndRearm,
};

// The subtraction is unsigned, so it is correct across esp_timer's
// millisecond-counter wraparound (~49.7 days) without special-casing it.
inline WakeWordCooldownDecision EvaluateWakeWordCooldown(uint32_t now_ms, uint32_t closed_at_ms,
                                                           uint32_t cooldown_ms) {
    return (now_ms - closed_at_ms) < cooldown_ms ? WakeWordCooldownDecision::kSuppressAndRearm
                                                  : WakeWordCooldownDecision::kProceed;
}

#endif  // WAKE_WORD_COOLDOWN_H
