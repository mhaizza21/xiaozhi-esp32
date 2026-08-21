#include "wake_word_threshold.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>

int main() {
    // No board override (the default for every board except Freenove):
    // must skip set_wakenet_threshold(), preserving the WakeNet model default.
    assert(!ResolveWakeNetThreshold(std::nullopt).has_value());

    // Freenove's hardware-validated override passes through unchanged.
    {
        auto resolved = ResolveWakeNetThreshold(0.4f);
        assert(resolved.has_value());
        assert(std::fabs(*resolved - 0.4f) < 1e-6f);
    }

    // The accepted range's upper bound also passes through.
    {
        auto resolved = ResolveWakeNetThreshold(0.9999f);
        assert(resolved.has_value());
        assert(std::fabs(*resolved - 0.9999f) < 1e-6f);
    }

    // A mid-range override is accepted as-is.
    {
        auto resolved = ResolveWakeNetThreshold(0.63f);
        assert(resolved.has_value());
        assert(std::fabs(*resolved - 0.63f) < 1e-6f);
    }

    // Below the accepted range: rejected rather than silently clamped, so an
    // obviously-wrong board value fails safe (model default) instead of
    // producing an out-of-spec call into set_wakenet_threshold().
    assert(!ResolveWakeNetThreshold(0.0f).has_value());
    assert(!ResolveWakeNetThreshold(0.39f).has_value());

    // Above the accepted range: also rejected.
    assert(!ResolveWakeNetThreshold(1.0f).has_value());
    assert(!ResolveWakeNetThreshold(2.5f).has_value());

    // Non-finite overrides: rejected, not silently let through by a plain
    // range comparison. NaN is the important case -- `NaN < 0.4f` and
    // `NaN > 0.9999f` are both false, so this only passes if the resolver
    // uses an explicit std::isfinite() check rather than relying on the
    // range comparison alone.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    assert(!ResolveWakeNetThreshold(nan).has_value());
    assert(!ResolveWakeNetThreshold(inf).has_value());
    assert(!ResolveWakeNetThreshold(-inf).has_value());

    return 0;
}
