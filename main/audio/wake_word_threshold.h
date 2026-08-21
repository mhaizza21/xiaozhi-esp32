#ifndef WAKE_WORD_THRESHOLD_H
#define WAKE_WORD_THRESHOLD_H

#include <cmath>
#include <optional>

// esp-sr's set_wakenet_threshold() only accepts values in [0.4, 0.9999].
// Boards signal "no override, use WakeNet's own model default" via
// Board::GetWakeNetThreshold() returning std::nullopt.
//
// Returns the threshold to apply, or std::nullopt if the caller should skip
// the set_wakenet_threshold() call entirely (no override, an override
// outside the accepted range, or a non-finite override). NaN is checked
// explicitly: `NaN < 0.4f` and `NaN > 0.9999f` are both false, so a plain
// range comparison would let NaN slip through unrejected.
inline std::optional<float> ResolveWakeNetThreshold(std::optional<float> board_threshold) {
    if (!board_threshold.has_value()) {
        return std::nullopt;
    }
    if (!std::isfinite(*board_threshold)) {
        return std::nullopt;
    }
    if (*board_threshold < 0.4f || *board_threshold > 0.9999f) {
        return std::nullopt;
    }
    return board_threshold;
}

#endif  // WAKE_WORD_THRESHOLD_H
