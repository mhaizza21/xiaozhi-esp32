#ifndef MHAIBOT_TEST_LEGACY_POSE_REPLICA_H
#define MHAIBOT_TEST_LEGACY_POSE_REPLICA_H

#include "eye_pose_adapter.h"

#include <cstdint>

// Slice 9 (09 "Shadow dual-path compare"): host-only, LVGL-free replica of
// MhaiBotFaceV2's PRIVATE pure-math pose functions. This file is test
// infrastructure under scripts/tests/ only — it is not compiled into the
// board build, does not modify main/boards/.../mhaibot_face_v2.cc in any
// way, and legacy remains the sole pixel authority (mhaibot_face_v2.cc's
// ApplyPose/ResolveRenderedPose/ResolveBasePose/InterpolatePose/
// PettingPose/StartledPose/GroggyPose are untouched).
//
// WHY A REPLICA, NOT THE REAL CLASS: MhaiBotFaceV2 unconditionally
// `#include <lvgl.h>` and cannot be host-compiled with the sandbox's
// available compilers (esp-clang, an ESP32 cross-compiler with no LVGL on
// the include path). The pose math itself (ResolveBasePose/InterpolatePose/
// PettingPose/StartledPose/GroggyPose/ClampProgress/LerpInt) touches no
// LVGL state — it is pure arithmetic over `config_`/`target_emotion_` — so
// it can be replicated verbatim here for host-side parity testing.
//
// CONTRACT: every function below must stay byte-identical (same operations,
// same operand order, same integer-truncation behavior) to its named
// counterpart in mhaibot_face_v2.cc. Line numbers below reference the state
// as of Slice 8 commit 40f990a; re-verify this file against
// mhaibot_face_v2.cc on any future legacy pose-math edit — a silent drift
// here would make the Slice 9 parity harness compare against a fiction
// instead of legacy's real behavior.

namespace LegacyPoseReplica {

// Mirrors MhaiBotFaceV2::Emotion (mhaibot_face_v2.h:30-41) — same 10 values,
// same order.
enum class Emotion {
    kNeutral,
    kRobot2,
    kHappy,
    kThinking,
    kSpeaking,
    kListening,
    kRelaxed,
    kConfident,
    kSleepy,
    kSleeping,
};

// Mirrors MhaiBotFaceV2::Config defaults (mhaibot_face_v2.h:17-28) — the
// only Config the real Freenove board uses.
struct Config {
    int root_width = 240;
    int root_height = 180;
    int vertical_offset = -2;
    int eye_width = 52;
    int eye_height = 62;
    int eye_gap = 42;
    int eye_y = 58;
    int corner_radius = 16;
    uint32_t tick_ms = 33;
    uint32_t transition_ms = 300;
};

// Mirrors MhaiBotFaceV2::ClampProgress (mhaibot_face_v2.cc:490-495).
inline uint16_t ClampProgress(uint32_t elapsed_ms, uint32_t duration_ms) {
    if (duration_ms == 0 || elapsed_ms >= duration_ms) {
        return 1000;
    }
    return static_cast<uint16_t>((elapsed_ms * 1000U) / duration_ms);
}

// Mirrors MhaiBotFaceV2::LerpInt (mhaibot_face_v2.cc:497-499). Integer
// truncation toward zero, exactly as C++ int division — this is the source
// of the <=1px tolerance in the Slice 9 comparison contract vs. the shadow
// path's float LerpFloat.
inline int LerpInt(int from, int to, uint16_t progress_per_mille) {
    return from + ((to - from) * static_cast<int>(progress_per_mille)) / 1000;
}

// Mirrors MhaiBotFaceV2::ResolveBasePose (mhaibot_face_v2.cc:375-410).
inline FaceV2Pose ResolveBasePose(const Config& config, Emotion emotion) {
    const int pair_width = config.eye_width * 2 + config.eye_gap;
    const int left = (config.root_width - pair_width) / 2;
    const int right = left + config.eye_width + config.eye_gap;
    const FaceV2Pose neutral{left, right, config.eye_y, config.eye_width, config.eye_height,
                              config.corner_radius};

    switch (emotion) {
        case Emotion::kHappy:
            return {left, right, config.eye_y + 8, config.eye_width, 34, config.corner_radius};
        case Emotion::kThinking:
            return {left - 8, right - 8, config.eye_y + 3, config.eye_width, 46, config.corner_radius};
        case Emotion::kSpeaking:
            return {left, right, config.eye_y + 2, config.eye_width, 54, config.corner_radius};
        case Emotion::kListening:
            return {left - 5,
                    right + 5,
                    config.eye_y - 4,
                    config.eye_width + 10,
                    config.eye_height + 8,
                    config.corner_radius};
        case Emotion::kRelaxed:
        case Emotion::kSleepy:
            return {left, right, config.eye_y + 13, config.eye_width, 24, config.corner_radius};
        case Emotion::kSleeping:
            return {left, right, config.eye_y + 22, config.eye_width, 8, config.corner_radius};
        case Emotion::kConfident:
            return {left + 4, right - 4, config.eye_y + 2, config.eye_width, 48, config.corner_radius};
        case Emotion::kRobot2:
        case Emotion::kNeutral:
        default:
            return neutral;
    }
}

// Mirrors MhaiBotFaceV2::InterpolatePose (mhaibot_face_v2.cc:440-450).
inline FaceV2Pose InterpolatePose(const FaceV2Pose& from, const FaceV2Pose& to,
                                   uint16_t progress_per_mille) {
    return {
        LerpInt(from.left_x, to.left_x, progress_per_mille),
        LerpInt(from.right_x, to.right_x, progress_per_mille),
        LerpInt(from.y, to.y, progress_per_mille),
        LerpInt(from.width, to.width, progress_per_mille),
        LerpInt(from.height, to.height, progress_per_mille),
        LerpInt(from.radius, to.radius, progress_per_mille),
    };
}

// Mirrors MhaiBotFaceV2::PettingPose (mhaibot_face_v2.cc:452-461).
inline FaceV2Pose PettingPose(const Config& config) {
    FaceV2Pose pose = ResolveBasePose(config, Emotion::kHappy);
    pose.height = 10;
    pose.radius = pose.height / 2;
    pose.y += 12;
    return pose;
}

// Mirrors MhaiBotFaceV2::StartledPose (mhaibot_face_v2.cc:463-471).
// Legacy reads `target_emotion_` as a member; the replica takes it as an
// explicit parameter since it has no persistent object.
inline FaceV2Pose StartledPose(const Config& config, Emotion target_emotion) {
    FaceV2Pose pose = ResolveBasePose(config, target_emotion);
    pose.left_x -= 7;
    pose.right_x += 7;
    pose.width += 14;
    pose.height += 20;
    pose.y -= 8;
    return pose;
}

// Mirrors MhaiBotFaceV2::GroggyPose (mhaibot_face_v2.cc:473-488). Legacy
// calls MhaiBotGroggyProgressPerMille(elapsed_ms)
// (mhaibot_interaction_model.cc:200-205), which is arithmetically identical
// to ClampProgress(elapsed_ms, 5000) — both truncate
// `(elapsed_ms * 1000) / duration` and both clamp to 1000 at/after
// duration, with the same 5000ms duration
// (mhaibot_interaction_model.cc:15 kGroggyWakeDurationMs).
inline FaceV2Pose GroggyPose(const Config& config, uint32_t elapsed_ms) {
    const uint16_t progress = ClampProgress(elapsed_ms, 5000);
    const FaceV2Pose sleeping = ResolveBasePose(config, Emotion::kSleeping);
    const FaceV2Pose sleepy = ResolveBasePose(config, Emotion::kSleepy);
    const FaceV2Pose neutral = ResolveBasePose(config, Emotion::kNeutral);

    FaceV2Pose pose = progress <= 300
                           ? InterpolatePose(sleeping, sleepy, progress * 1000U / 300U)
                           : InterpolatePose(sleepy, neutral, (progress - 300U) * 1000U / 700U);

    const uint16_t blink = progress < 150 ? (1000U - progress * 4U) : 400U;
    if (pose.height > static_cast<int>(blink / 10U)) {
        pose.height = static_cast<int>(blink / 10U);
    }
    return pose;
}

}  // namespace LegacyPoseReplica

#endif  // MHAIBOT_TEST_LEGACY_POSE_REPLICA_H
