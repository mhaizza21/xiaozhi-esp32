#include "emotion_controller.h"

namespace {
// Matches MhaiBotFaceV2::Config's defaults exactly (the only Config the
// real Freenove board uses), so the Neutral/Happy/Focused/Sleepy cases
// below are byte-for-byte equivalent (as floats) to the corresponding
// MhaiBotFaceV2::ResolveBasePose output today — extracted, not restyled
// (09 Slice 6). EmotionController stays LVGL-free/board-Config-free per
// 02's documented API; bridging a runtime baseline is left for a
// coordinator to add if it becomes necessary (Slice 7+).
constexpr float kRootWidth = 240.0f;
constexpr float kEyeWidth = 52.0f;
constexpr float kEyeHeight = 62.0f;
constexpr float kEyeGap = 42.0f;
constexpr float kEyeY = 58.0f;
constexpr float kCornerRadius = 16.0f;

constexpr float kPairWidth = kEyeWidth * 2.0f + kEyeGap;
constexpr float kLeftX = (kRootWidth - kPairWidth) / 2.0f;
constexpr float kRightX = kLeftX + kEyeWidth + kEyeGap;
constexpr float kNeutralCenterXLeft = kLeftX + kEyeWidth * 0.5f;
constexpr float kNeutralCenterXRight = kRightX + kEyeWidth * 0.5f;

constexpr float CenterY(float top_y, float height) {
    return top_y + height * 0.5f;
}

// Happy: matches legacy kHappy (eye_y+8, height 34).
constexpr float kHappyY = kEyeY + 8.0f;
constexpr float kHappyHeight = 34.0f;
constexpr float kHappyCenterY = CenterY(kHappyY, kHappyHeight);

// Focused: matches legacy kConfident — the activity-neutral member of
// legacy's Thinking/Listening/Confident trio that all collapse to Focused
// per ADR-002 (left+4, right-4, eye_y+2, height 48). Thinking/Listening's
// extra deltas are activity tweaks reserved for Slice 7, not part of the
// emotion base pose.
constexpr float kFocusedY = kEyeY + 2.0f;
constexpr float kFocusedHeight = 48.0f;
constexpr float kFocusedCenterY = CenterY(kFocusedY, kFocusedHeight);
constexpr float kFocusedCenterXLeft = kNeutralCenterXLeft + 4.0f;
constexpr float kFocusedCenterXRight = kNeutralCenterXRight - 4.0f;

// Sleepy: matches legacy kRelaxed/kSleepy (eye_y+13, height 24). The
// deeper kSleeping closure (eye_y+22, height 8) is the Sleeping *activity*
// override, not the Sleepy *emotion* base — that distinction is a Slice 7
// activity tweak, not part of this base pose.
constexpr float kSleepyY = kEyeY + 13.0f;
constexpr float kSleepyHeight = 24.0f;
constexpr float kSleepyCenterY = CenterY(kSleepyY, kSleepyHeight);

// Neutral: matches legacy kNeutral/kRobot2.
constexpr float kNeutralCenterY = CenterY(kEyeY, kEyeHeight);

// Sad/Angry/Surprised have no legacy precedent — MhaiBotFaceV2::Emotion has
// no corresponding values, so legacy never rendered them (zero pixel-
// regression risk). Geometry below follows the qualitative descriptions in
// 07 §4.3/4.4/4.5 and is a new tuning target pending hardware validation,
// same category as prior slices' provisional constants (blink timing,
// idle gaze scale).
constexpr float kSadY = kEyeY + 10.0f;   // reduced openness, downward weight
constexpr float kSadHeight = 30.0f;
constexpr float kSadCenterY = CenterY(kSadY, kSadHeight);

constexpr float kAngryY = kEyeY + 6.0f;  // focused, narrower shape
constexpr float kAngryHeight = 40.0f;
constexpr float kAngryWidth = 40.0f;
constexpr float kAngryCenterY = CenterY(kAngryY, kAngryHeight);

constexpr float kSurprisedY = kEyeY - 6.0f;  // increased height/openness
constexpr float kSurprisedHeight = 82.0f;
constexpr float kSurprisedWidth = 58.0f;
constexpr float kSurprisedCenterY = CenterY(kSurprisedY, kSurprisedHeight);
constexpr float kSurprisedCenterXLeft = kNeutralCenterXLeft - 5.0f;
constexpr float kSurprisedCenterXRight = kNeutralCenterXRight + 5.0f;

EyeGeometry MakeGeometry(float center_x, float center_y, float width, float height,
                          float corner_radius) {
    EyeGeometry geometry{};
    geometry.center_x = center_x;
    geometry.center_y = center_y;
    geometry.width = width;
    geometry.height = height;
    geometry.corner_radius = corner_radius;
    geometry.rotation_degrees = 0.0f;
    return geometry;
}
}  // namespace

EmotionController::EmotionController() {
    SetEmotion(EyeEmotion::Neutral);
}

void EmotionController::SetEmotion(EyeEmotion emotion) {
    if (emotion_ == emotion) {
        return;
    }
    emotion_ = emotion;
    frame_ = BasePose(emotion);
}

void EmotionController::Update(uint32_t delta_ms) {
    // No-op today (see header): the base pose is an instant lookup, not an
    // interpolation. delta_ms is accepted to match the documented
    // time-based controller API and is reserved for the Slice 8 transition
    // migration (inventory T5).
    (void)delta_ms;
}

EyeFrame EmotionController::BasePose(EyeEmotion emotion) {
    EyeFrame frame{};
    switch (emotion) {
        case EyeEmotion::Happy:
            frame.left =
                MakeGeometry(kNeutralCenterXLeft, kHappyCenterY, kEyeWidth, kHappyHeight, kCornerRadius);
            frame.right = MakeGeometry(kNeutralCenterXRight, kHappyCenterY, kEyeWidth, kHappyHeight,
                                       kCornerRadius);
            break;
        case EyeEmotion::Sad:
            frame.left =
                MakeGeometry(kNeutralCenterXLeft, kSadCenterY, kEyeWidth, kSadHeight, kCornerRadius);
            frame.right =
                MakeGeometry(kNeutralCenterXRight, kSadCenterY, kEyeWidth, kSadHeight, kCornerRadius);
            break;
        case EyeEmotion::Angry:
            frame.left =
                MakeGeometry(kNeutralCenterXLeft, kAngryCenterY, kAngryWidth, kAngryHeight, kCornerRadius);
            frame.right = MakeGeometry(kNeutralCenterXRight, kAngryCenterY, kAngryWidth, kAngryHeight,
                                       kCornerRadius);
            break;
        case EyeEmotion::Surprised:
            frame.left = MakeGeometry(kSurprisedCenterXLeft, kSurprisedCenterY, kSurprisedWidth,
                                       kSurprisedHeight, kCornerRadius);
            frame.right = MakeGeometry(kSurprisedCenterXRight, kSurprisedCenterY, kSurprisedWidth,
                                        kSurprisedHeight, kCornerRadius);
            break;
        case EyeEmotion::Focused:
            frame.left = MakeGeometry(kFocusedCenterXLeft, kFocusedCenterY, kEyeWidth, kFocusedHeight,
                                       kCornerRadius);
            frame.right = MakeGeometry(kFocusedCenterXRight, kFocusedCenterY, kEyeWidth, kFocusedHeight,
                                        kCornerRadius);
            break;
        case EyeEmotion::Sleepy:
            frame.left =
                MakeGeometry(kNeutralCenterXLeft, kSleepyCenterY, kEyeWidth, kSleepyHeight, kCornerRadius);
            frame.right = MakeGeometry(kNeutralCenterXRight, kSleepyCenterY, kEyeWidth, kSleepyHeight,
                                       kCornerRadius);
            break;
        case EyeEmotion::Neutral:
        default:
            frame.left =
                MakeGeometry(kNeutralCenterXLeft, kNeutralCenterY, kEyeWidth, kEyeHeight, kCornerRadius);
            frame.right =
                MakeGeometry(kNeutralCenterXRight, kNeutralCenterY, kEyeWidth, kEyeHeight, kCornerRadius);
            break;
    }
    frame.opacity = 1.0f;
    return frame;
}
