#include "wake_word_cooldown.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    constexpr uint32_t kCooldownMs = 1500;

    // Just after close: suppress and signal re-arm.
    assert(EvaluateWakeWordCooldown(/*now_ms=*/0, /*closed_at_ms=*/0, kCooldownMs) ==
           WakeWordCooldownDecision::kSuppressAndRearm);
    assert(EvaluateWakeWordCooldown(1499, 0, kCooldownMs) == WakeWordCooldownDecision::kSuppressAndRearm);

    // Exact boundary: cooldown window is [closed_at_ms, closed_at_ms + cooldown_ms),
    // so `now_ms - closed_at_ms == cooldown_ms` is the first proceed instant.
    assert(EvaluateWakeWordCooldown(1500, 0, kCooldownMs) == WakeWordCooldownDecision::kProceed);
    assert(EvaluateWakeWordCooldown(1501, 0, kCooldownMs) == WakeWordCooldownDecision::kProceed);

    // Well past the cooldown window.
    assert(EvaluateWakeWordCooldown(100000, 0, kCooldownMs) == WakeWordCooldownDecision::kProceed);

    // No channel close yet (closed_at_ms_ default-initialized to 0) and the
    // device has been up longer than the cooldown: must not suppress forever.
    assert(EvaluateWakeWordCooldown(50000, 0, kCooldownMs) == WakeWordCooldownDecision::kProceed);

    // esp_timer's millisecond counter wraps around (~49.7 days); unsigned
    // subtraction must still yield the correct elapsed time across the wrap.
    const uint32_t near_max = std::numeric_limits<uint32_t>::max();
    // closed_at_ms_ just before the wrap, now_ms just after it: well within cooldown.
    assert(EvaluateWakeWordCooldown(9, near_max - 1, kCooldownMs) ==
           WakeWordCooldownDecision::kSuppressAndRearm);
    // Same wrap, but far enough past close that the cooldown has expired.
    assert(EvaluateWakeWordCooldown(near_max - 1 + kCooldownMs + 1, near_max - 1, kCooldownMs) ==
           WakeWordCooldownDecision::kProceed);

    // Repeated wake words within one cooldown window each re-arm (decision is
    // stateless and re-evaluates against the same closed_at_ms_ every call).
    for (uint32_t now_ms = 0; now_ms < kCooldownMs; now_ms += 100) {
        assert(EvaluateWakeWordCooldown(now_ms, 0, kCooldownMs) ==
               WakeWordCooldownDecision::kSuppressAndRearm);
    }

    return 0;
}
