#ifndef MHAIBOT_LVGL_EYE_RENDERER_H
#define MHAIBOT_LVGL_EYE_RENDERER_H

#include "eye_frame.h"

#include <lvgl.h>

// Applies EyeFrame geometry to pre-created LVGL eye objects (06 §4.5 / §6).
// Binds object references only; does not own object lifetime, sleep_label_,
// or Show/Hide (ADR-003 — face shell / MhaiBotFaceV2 owns those). Must be
// called only from the existing LVGL-safe display/tick context (06 §8).
class LVGLEyeRenderer {
public:
    // Bind to objects already created elsewhere. Does not take ownership.
    void Init(lv_obj_t* left_eye, lv_obj_t* right_eye);

    // Apply frame geometry/opacity to the bound eye objects. Non-finite
    // fields fall back to the last valid frame (06 §10); if no valid frame
    // has ever been rendered, the call is skipped.
    void Render(const EyeFrame& frame);

    // Drop object references (does not delete LVGL objects).
    void Deinit();

private:
    static bool GeometryFinite(const EyeGeometry& geometry);
    void ApplyEye(lv_obj_t* eye, const EyeGeometry& geometry, lv_opa_t opa);

    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    EyeFrame last_valid_frame_{};
    bool has_valid_frame_ = false;
    bool warned_uninitialized_ = false;
};

#endif  // MHAIBOT_LVGL_EYE_RENDERER_H
