#ifndef MHAIBOT_EYE_FRAME_H
#define MHAIBOT_EYE_FRAME_H

// Stereo eye geometry for the Freenove MhaiBot extract (06 / 02).
// Openness is composed into height before emit (ADR-004); not stored here.

struct EyeGeometry {
    float center_x = 0.0f;
    float center_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float corner_radius = 0.0f;
    float rotation_degrees = 0.0f;
};

struct EyeFrame {
    EyeGeometry left{};
    EyeGeometry right{};
    float opacity = 1.0f;
};

#endif  // MHAIBOT_EYE_FRAME_H
