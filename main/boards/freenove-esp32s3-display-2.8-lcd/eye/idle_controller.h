#ifndef MHAIBOT_IDLE_CONTROLLER_H
#define MHAIBOT_IDLE_CONTROLLER_H

#include <cstdint>

// LVGL-free idle micro-gaze controller (02 / 07 §6). Owns event timing and
// the resulting normalized look offset only; the shared post-compose stage
// (ApplyIdleGaze) converts it to a pixel shift on EyeFrame — IdleController
// never touches EyeFrame or LVGL directly.
//
// Event-driven, not per-frame RNG (07 §6 / §10): a small seeded PRNG picks
// the wait interval and target offset only when a new event begins, so
// behavior is reproducible for a fixed seed. Combines the fully-specified
// numeric ranges from 07: amplitude/hold duration from §6.1 (Micro Gaze),
// move/return transition duration from §8 ("Gaze movement"), and event
// interval from §6.2 (Idle Glance) — 07 §6.4's weighted multi-event
// selection is documented as a tuning default, not a hard requirement, and
// is not implemented here.
class IdleController {
public:
    explicit IdleController(uint32_t rng_seed = kDefaultRngSeed);

    // Enable (Idle activity) or disable (every other activity/transient —
    // 05 §5, 07 §7) idle motion. On disable, any in-flight offset eases
    // back to center instead of freezing off-axis, since a stale idle
    // offset would conflict with poses that expect a centered gaze (07
    // §7.2). Edge-triggered: repeated calls with the same value are cheap
    // no-ops so the caller may call this every tick.
    void SetEnabled(bool enabled);

    void Update(uint32_t delta_ms);

    // Normalized offsets per 07 §3 (-1..1); bounded to the 07 §6.1 Micro
    // Gaze range in practice (look_x: ±0.15, look_y: ±0.08).
    float look_offset_x() const { return look_offset_x_; }
    float look_offset_y() const { return look_offset_y_; }

private:
    enum class Phase { kWaiting, kMoving, kHolding, kReturning };

    static constexpr uint32_t kDefaultRngSeed = 0xB5297A4Du;

    void BeginEvent();
    uint32_t NextEventIntervalMs();
    float NextOffsetComponent(float max_abs);
    static float LerpFloat(float from, float to, uint16_t progress_per_mille);

    bool enabled_ = true;
    Phase phase_ = Phase::kWaiting;
    uint32_t phase_elapsed_ms_ = 0;
    uint32_t wait_elapsed_ms_ = 0;
    uint32_t wait_interval_ms_;
    float origin_offset_x_ = 0.0f;
    float origin_offset_y_ = 0.0f;
    float target_offset_x_ = 0.0f;
    float target_offset_y_ = 0.0f;
    float look_offset_x_ = 0.0f;
    float look_offset_y_ = 0.0f;
    uint32_t rng_state_;
};

#endif  // MHAIBOT_IDLE_CONTROLLER_H
