#include "lvgl_eye_renderer.h"

#include <esp_log.h>

#include <cmath>

#define TAG "LVGLEyeRenderer"

void LVGLEyeRenderer::Init(lv_obj_t* left_eye, lv_obj_t* right_eye) {
    left_eye_ = left_eye;
    right_eye_ = right_eye;
    has_valid_frame_ = false;
    warned_uninitialized_ = false;
}

void LVGLEyeRenderer::Deinit() {
    left_eye_ = nullptr;
    right_eye_ = nullptr;
    has_valid_frame_ = false;
}

bool LVGLEyeRenderer::GeometryFinite(const EyeGeometry& geometry) {
    return std::isfinite(geometry.center_x) && std::isfinite(geometry.center_y) &&
           std::isfinite(geometry.width) && std::isfinite(geometry.height) &&
           std::isfinite(geometry.corner_radius) && std::isfinite(geometry.rotation_degrees);
}

void LVGLEyeRenderer::Render(const EyeFrame& frame) {
    if (left_eye_ == nullptr || right_eye_ == nullptr || !lv_obj_is_valid(left_eye_) ||
        !lv_obj_is_valid(right_eye_)) {
        if (!warned_uninitialized_) {
            ESP_LOGW(TAG, "Render called without bound/valid eye objects; skipping");
            warned_uninitialized_ = true;
        }
        return;
    }

    const bool finite = GeometryFinite(frame.left) && GeometryFinite(frame.right) &&
                         std::isfinite(frame.opacity);
    if (!finite) {
        if (!has_valid_frame_) {
            ESP_LOGW(TAG, "Non-finite EyeFrame with no prior valid frame; skipping render");
            return;
        }
        ESP_LOGW(TAG, "Non-finite EyeFrame; keeping last valid frame");
    } else {
        last_valid_frame_ = frame;
        has_valid_frame_ = true;
    }

    const EyeFrame& safe = last_valid_frame_;
    const float opacity = std::fmin(std::fmax(safe.opacity, 0.0f), 1.0f);
    const lv_opa_t opa = static_cast<lv_opa_t>(std::lround(opacity * 255.0f));

    ApplyEye(left_eye_, safe.left, opa);
    ApplyEye(right_eye_, safe.right, opa);
}

void LVGLEyeRenderer::ApplyEye(lv_obj_t* eye, const EyeGeometry& geometry, lv_opa_t opa) {
    if (eye == nullptr || !lv_obj_is_valid(eye)) {
        return;
    }

    const int32_t width = static_cast<int32_t>(std::lround(std::fmax(geometry.width, 0.0f)));
    const int32_t height = static_cast<int32_t>(std::lround(std::fmax(geometry.height, 0.0f)));
    const int32_t radius = static_cast<int32_t>(std::lround(std::fmax(geometry.corner_radius, 0.0f)));
    const int32_t x = static_cast<int32_t>(std::lround(geometry.center_x - geometry.width * 0.5f));
    const int32_t y = static_cast<int32_t>(std::lround(geometry.center_y - geometry.height * 0.5f));

    lv_obj_set_size(eye, width, height);
    lv_obj_set_style_radius(eye, radius, 0);
    lv_obj_set_pos(eye, x, y);
    lv_obj_set_style_bg_opa(eye, opa, 0);
}
