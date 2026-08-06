#ifndef MHAIBOT_EYE_POSE_ADAPTER_H
#define MHAIBOT_EYE_POSE_ADAPTER_H

#include "eye_frame.h"

#include <cmath>

// Field-identical to MhaiBotFaceV2::Pose: LVGL top-left origins, shared y.
// Kept LVGL-free so host round-trip tests can include this header alone.
struct FaceV2Pose {
    int left_x = 0;
    int right_x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int radius = 0;
};

// Round-trip policy: Pose → EyeFrame is exact float promotion of integer
// fields (centers = origin + size/2). EyeFrame → Pose uses std::lround;
// integer Pose values that originated from FaceV2Pose round-trip exactly
// (±0.5 px tolerance only if EyeFrame was edited in float space).

inline EyeFrame PoseToEyeFrame(const FaceV2Pose& pose, float opacity = 1.0f) {
    EyeFrame frame{};
    const float half_w = static_cast<float>(pose.width) * 0.5f;
    const float half_h = static_cast<float>(pose.height) * 0.5f;
    const float y_center = static_cast<float>(pose.y) + half_h;
    const float w = static_cast<float>(pose.width);
    const float h = static_cast<float>(pose.height);
    const float r = static_cast<float>(pose.radius);

    frame.left.center_x = static_cast<float>(pose.left_x) + half_w;
    frame.left.center_y = y_center;
    frame.left.width = w;
    frame.left.height = h;
    frame.left.corner_radius = r;
    frame.left.rotation_degrees = 0.0f;

    frame.right.center_x = static_cast<float>(pose.right_x) + half_w;
    frame.right.center_y = y_center;
    frame.right.width = w;
    frame.right.height = h;
    frame.right.corner_radius = r;
    frame.right.rotation_degrees = 0.0f;

    frame.opacity = opacity;
    return frame;
}

inline FaceV2Pose EyeFrameToPose(const EyeFrame& frame) {
    FaceV2Pose pose{};
    pose.width = static_cast<int>(std::lround(frame.left.width));
    pose.height = static_cast<int>(std::lround(frame.left.height));
    pose.radius = static_cast<int>(std::lround(frame.left.corner_radius));
    pose.left_x = static_cast<int>(std::lround(frame.left.center_x - frame.left.width * 0.5f));
    pose.right_x = static_cast<int>(std::lround(frame.right.center_x - frame.right.width * 0.5f));
    pose.y = static_cast<int>(std::lround(frame.left.center_y - frame.left.height * 0.5f));
    return pose;
}

#endif  // MHAIBOT_EYE_POSE_ADAPTER_H
