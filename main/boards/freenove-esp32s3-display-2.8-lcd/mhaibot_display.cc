#include "mhaibot_display.h"

#include "lvgl_theme.h"

#include <esp_log.h>

#include <cstring>

#define TAG "MhaiBotDisplay"

namespace {
// Exceptional states that still use the legacy emoji/GIF set. Everything
// else — the named MhaiBot face emotions and any unrecognized string — keeps
// the face on screen.
constexpr const char* kLegacyEmotions[] = {
    "warning", "error", "sad", "crying", "angry", "surprised", "shocked", "cancel",
};

const char* FaceEmotionName(MhaiBotFace::Emotion emotion) {
    switch (emotion) {
        case MhaiBotFace::Emotion::kNeutral:
            return "neutral";
        case MhaiBotFace::Emotion::kRobot2:
            return "robot_2";
        case MhaiBotFace::Emotion::kHappy:
            return "happy";
        case MhaiBotFace::Emotion::kThinking:
            return "thinking";
        case MhaiBotFace::Emotion::kSpeaking:
            return "speaking";
        case MhaiBotFace::Emotion::kListening:
            return "listening";
        case MhaiBotFace::Emotion::kRelaxed:
            return "relaxed";
        case MhaiBotFace::Emotion::kConfident:
            return "confident";
    }
    return "unknown";
}
}  // namespace

MhaiBotDisplay::MhaiBotDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

MhaiBotDisplay::~MhaiBotDisplay() {
    // Tear down the face (LVGL timer + objects) under the LVGL lock before the
    // base LcdDisplay destructor deletes container_/display_.
    DisplayLockGuard lock(this);
    face_.reset();
}

bool MhaiBotDisplay::IsLegacyEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return false;
    }
    for (const char* legacy : kLegacyEmotions) {
        if (strcmp(emotion, legacy) == 0) {
            return true;
        }
    }
    return false;
}

MhaiBotFace::Emotion MhaiBotDisplay::ToFaceEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return MhaiBotFace::Emotion::kNeutral;
    }
    if (strcmp(emotion, "robot_2") == 0) {
        return MhaiBotFace::Emotion::kRobot2;
    }
    if (strcmp(emotion, "happy") == 0) {
        return MhaiBotFace::Emotion::kHappy;
    }
    // "laughing" reuses the Happy geometry for now; give it its own Emotion
    // value only if it later needs a visually distinct expression.
    if (strcmp(emotion, "laughing") == 0) {
        return MhaiBotFace::Emotion::kHappy;
    }
    if (strcmp(emotion, "thinking") == 0) {
        return MhaiBotFace::Emotion::kThinking;
    }
    // "confused" reuses the Thinking geometry/glance for now; give it its own
    // Emotion value only if it later needs a visually distinct expression.
    if (strcmp(emotion, "confused") == 0) {
        return MhaiBotFace::Emotion::kThinking;
    }
    if (strcmp(emotion, "speaking") == 0) {
        return MhaiBotFace::Emotion::kSpeaking;
    }
    if (strcmp(emotion, "listening") == 0) {
        return MhaiBotFace::Emotion::kListening;
    }
    if (strcmp(emotion, "relaxed") == 0) {
        return MhaiBotFace::Emotion::kRelaxed;
    }
    if (strcmp(emotion, "confident") == 0) {
        return MhaiBotFace::Emotion::kConfident;
    }
    // "neutral" and any unrecognized emotion default to the neutral face.
    return MhaiBotFace::Emotion::kNeutral;
}

void MhaiBotDisplay::LogUnknownEmotionOnce(const char* emotion) {
    if (emotion == nullptr) {
        return;
    }
    static constexpr const char* kKnownFaceEmotions[] = {
        "neutral", "robot_2", "happy", "laughing", "thinking", "confused",
        "speaking", "listening", "relaxed", "confident",
    };
    for (const char* known : kKnownFaceEmotions) {
        if (strcmp(emotion, known) == 0) {
            return;
        }
    }
    if (logged_unknown_emotions_.insert(emotion).second) {
        ESP_LOGW(TAG, "Unknown emotion '%s', defaulting to MhaiBot face", emotion);
    }
}

void MhaiBotDisplay::LogEmotionTransition(const char* emotion, MhaiBotFace::Emotion mapped) {
    const std::string raw = emotion != nullptr ? emotion : "(null)";
    if (has_logged_emotion_ && raw == last_logged_emotion_) {
        return;
    }
    has_logged_emotion_ = true;
    last_logged_emotion_ = raw;
    ESP_LOGI(TAG, "Emotion '%s' -> face:%s", raw.c_str(), FaceEmotionName(mapped));
}

void MhaiBotDisplay::ApplyFaceVisibility() {
    if (face_ == nullptr) {
        return;
    }

    if (face_visible_) {
        if (emoji_box_ != nullptr && lv_obj_is_valid(emoji_box_)) {
            lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
        face_->Show();
    } else {
        face_->Hide();
        if (emoji_box_ != nullptr && lv_obj_is_valid(emoji_box_)) {
            lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void MhaiBotDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    if (container_ == nullptr) {
        ESP_LOGE(TAG, "container_ is null after SetupUI, face not created");
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const lv_color_t eye_color =
        lvgl_theme != nullptr ? lvgl_theme->text_color() : lv_color_hex(0x000000);

    face_ = std::make_unique<MhaiBotFace>(container_, eye_color);
    face_visible_ = true;
    ApplyFaceVisibility();
    ESP_LOGI(TAG, "MhaiBot face initialized");
}

void MhaiBotDisplay::SetEmotion(const char* emotion) {
    LogEmotionTransition(emotion, ToFaceEmotion(emotion));

    if (face_ == nullptr) {
        SpiLcdDisplay::SetEmotion(emotion);
        return;
    }

    if (IsLegacyEmotion(emotion)) {
        {
            DisplayLockGuard lock(this);
            face_visible_ = false;
            face_->Hide();
            if (emoji_box_ != nullptr && lv_obj_is_valid(emoji_box_)) {
                lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        SpiLcdDisplay::SetEmotion(emotion);
        return;
    }

    LogUnknownEmotionOnce(emotion);

    DisplayLockGuard lock(this);
    face_visible_ = true;
    face_->SetEmotion(ToFaceEmotion(emotion));
    // Stop any GIF before showing the face so LVGL does not keep animating
    // a hidden emoji image.
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    ApplyFaceVisibility();
}

void MhaiBotDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    // LcdDisplay's preview_timer_ callback invokes SetPreviewImage(nullptr)
    // virtually after PREVIEW_IMAGE_DURATION_MS. Overriding here restores
    // face/emoji visibility after the preview has truly ended, without
    // modifying lcd_display.cc.
    const bool showing_preview = (image != nullptr);
    SpiLcdDisplay::SetPreviewImage(std::move(image));

    DisplayLockGuard lock(this);
    if (face_ == nullptr) {
        return;
    }

    if (showing_preview) {
        // Keep the face hidden while the preview image is on screen.
        face_->Hide();
    } else {
        ApplyFaceVisibility();
    }
}

void MhaiBotDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);

    DisplayLockGuard lock(this);
    if (face_ == nullptr || theme == nullptr) {
        return;
    }
    auto* lvgl_theme = static_cast<LvglTheme*>(theme);
    face_->SetColor(lvgl_theme->text_color());
}
