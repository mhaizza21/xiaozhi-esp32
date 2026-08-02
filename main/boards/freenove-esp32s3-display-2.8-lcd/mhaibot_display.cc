#include "mhaibot_display.h"

#include "assets/lang_config.h"
#include "board.h"
#include "lvgl_theme.h"
#include "mhaibot_interaction_model.h"

#include <esp_err.h>
#include <esp_log.h>
#include <material_symbols.h>

#include <cstring>

#define TAG "MhaiBotDisplay"

namespace {
// Board-local eyes-only palette and minimal alert colors.
constexpr uint32_t kMhaiBotEyeColor = 0x00C8E0;
constexpr uint32_t kMhaiBotBackgroundColor = 0x000000;
constexpr uint32_t kMhaiBotErrorColor = 0xFF3B30;
constexpr uint32_t kMhaiBotBatteryColor = 0xFF8A00;

const char* FaceEmotionName(MhaiBotFaceV2::Emotion emotion) {
    switch (emotion) {
        case MhaiBotFaceV2::Emotion::kNeutral:
            return "neutral";
        case MhaiBotFaceV2::Emotion::kRobot2:
            return "robot_2";
        case MhaiBotFaceV2::Emotion::kHappy:
            return "happy";
        case MhaiBotFaceV2::Emotion::kThinking:
            return "thinking";
        case MhaiBotFaceV2::Emotion::kSpeaking:
            return "speaking";
        case MhaiBotFaceV2::Emotion::kListening:
            return "listening";
        case MhaiBotFaceV2::Emotion::kRelaxed:
            return "relaxed";
        case MhaiBotFaceV2::Emotion::kConfident:
            return "confident";
        case MhaiBotFaceV2::Emotion::kSleeping:
            return "sleeping";
        case MhaiBotFaceV2::Emotion::kSleepy:
            return "sleepy";
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
    if (reaction_emoji_timer_ != nullptr) {
        esp_timer_stop(reaction_emoji_timer_);
        esp_timer_delete(reaction_emoji_timer_);
        reaction_emoji_timer_ = nullptr;
    }
    // Tear down the face (LVGL timer + objects) under the LVGL lock before the
    // base LcdDisplay destructor deletes container_/display_.
    DisplayLockGuard lock(this);
    face_.reset();
}

bool MhaiBotDisplay::IsLegacyEmotion(const char* emotion) {
    (void)emotion;
    return false;
}

bool MhaiBotDisplay::IsErrorStatus(const char* status) {
    return status != nullptr && strcmp(status, Lang::Strings::ERROR) == 0;
}

bool MhaiBotDisplay::IsErrorEmotion(const char* emotion) {
    return emotion != nullptr && (strcmp(emotion, "error") == 0 || strcmp(emotion, "cancel") == 0 ||
                                  strcmp(emotion, "cloud_off") == 0);
}

bool MhaiBotDisplay::IsWarningEmotion(const char* emotion) {
    return emotion != nullptr && strcmp(emotion, "warning") == 0;
}

bool MhaiBotDisplay::IsNotificationEmotion(const char* emotion) {
    return emotion != nullptr &&
           (strcmp(emotion, "notification") == 0 || strcmp(emotion, "excited") == 0);
}

MhaiBotFaceV2::Emotion MhaiBotDisplay::ToFaceEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return MhaiBotFaceV2::Emotion::kNeutral;
    }
    if (strcmp(emotion, "robot_2") == 0) {
        return MhaiBotFaceV2::Emotion::kRobot2;
    }
    if (strcmp(emotion, "happy") == 0) {
        return MhaiBotFaceV2::Emotion::kHappy;
    }
    // "laughing" reuses the Happy geometry for now; give it its own Emotion
    // value only if it later needs a visually distinct expression.
    if (strcmp(emotion, "laughing") == 0) {
        return MhaiBotFaceV2::Emotion::kHappy;
    }
    if (strcmp(emotion, "thinking") == 0) {
        return MhaiBotFaceV2::Emotion::kThinking;
    }
    if (IsWarningEmotion(emotion)) {
        return MhaiBotFaceV2::Emotion::kThinking;
    }
    // "confused" reuses the Thinking geometry/glance for now; give it its own
    // Emotion value only if it later needs a visually distinct expression.
    if (strcmp(emotion, "confused") == 0) {
        return MhaiBotFaceV2::Emotion::kThinking;
    }
    if (strcmp(emotion, "speaking") == 0) {
        return MhaiBotFaceV2::Emotion::kSpeaking;
    }
    if (strcmp(emotion, "listening") == 0) {
        return MhaiBotFaceV2::Emotion::kListening;
    }
    if (strcmp(emotion, "relaxed") == 0) {
        return MhaiBotFaceV2::Emotion::kRelaxed;
    }
    if (strcmp(emotion, "confident") == 0) {
        return MhaiBotFaceV2::Emotion::kConfident;
    }
    if (IsNotificationEmotion(emotion)) {
        return MhaiBotFaceV2::Emotion::kHappy;
    }
    if (strcmp(emotion, "sleeping") == 0 || strcmp(emotion, "sleep") == 0) {
        return MhaiBotFaceV2::Emotion::kSleeping;
    }
    if (strcmp(emotion, "sleepy") == 0) {
        return MhaiBotFaceV2::Emotion::kSleepy;
    }
    // "neutral" and any unrecognized emotion default to the neutral face.
    return MhaiBotFaceV2::Emotion::kNeutral;
}

void MhaiBotDisplay::LogUnknownEmotionOnce(const char* emotion) {
    if (emotion == nullptr) {
        return;
    }
    static constexpr const char* kKnownFaceEmotions[] = {
        "neutral",  "robot_2",   "happy",        "laughing",  "thinking", "confused",
        "speaking", "listening", "relaxed",      "confident", "sleepy",   "sleeping",
        "sleep",    "warning",   "notification", "excited",
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

void MhaiBotDisplay::LogEmotionTransition(const char* emotion, MhaiBotFaceV2::Emotion mapped) {
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

void MhaiBotDisplay::ApplyEyesOnlyChrome() {
    const lv_color_t background = lv_color_hex(kMhaiBotBackgroundColor);

    if (container_ != nullptr && lv_obj_is_valid(container_)) {
        lv_obj_set_style_bg_color(container_, background, 0);
    }
    if (content_ != nullptr && lv_obj_is_valid(content_)) {
        lv_obj_set_style_bg_color(content_, background, 0);
        lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* objects[] = {
        top_bar_,           status_bar_,         bottom_bar_,
        low_battery_popup_, notification_label_, chat_message_label_,
        network_label_,     mute_label_,         battery_label_,
        status_label_,
    };
    for (lv_obj_t* object : objects) {
        if (object != nullptr && lv_obj_is_valid(object)) {
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
    }

    UpdateAlertLabel();
}

void MhaiBotDisplay::SetAlertState(bool error_active, bool battery_low) {
    error_active_ = error_active;
    battery_low_ = battery_low;
    UpdateAlertLabel();
}

void MhaiBotDisplay::UpdateAlertLabel() {
    if (alert_label_ == nullptr || !lv_obj_is_valid(alert_label_)) {
        return;
    }

    auto* lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const MhaiBotAlert alert = MhaiBotResolveAlert(error_active_, battery_low_);
    if (alert == MhaiBotAlert::kNone) {
        lv_obj_add_flag(alert_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (alert == MhaiBotAlert::kError) {
        lv_label_set_text(alert_label_, "!");
        if (lvgl_theme != nullptr && lvgl_theme->text_font() != nullptr) {
            lv_obj_set_style_text_font(alert_label_, lvgl_theme->text_font()->font(), 0);
        }
        lv_obj_set_style_text_color(alert_label_, lv_color_hex(kMhaiBotErrorColor), 0);
    } else {
        lv_label_set_text(alert_label_, MATERIAL_SYMBOLS_BATTERY_ANDROID_0);
        if (lvgl_theme != nullptr && lvgl_theme->large_icon_font() != nullptr) {
            lv_obj_set_style_text_font(alert_label_, lvgl_theme->large_icon_font()->font(), 0);
        }
        lv_obj_set_style_text_color(alert_label_, lv_color_hex(kMhaiBotBatteryColor), 0);
    }

    lv_obj_remove_flag(alert_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(alert_label_);
}

void MhaiBotDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    if (container_ == nullptr) {
        ESP_LOGE(TAG, "container_ is null after SetupUI, face not created");
        return;
    }

    const lv_color_t eye_color = lv_color_hex(kMhaiBotEyeColor);

    face_ = std::make_unique<MhaiBotFaceV2>(container_, eye_color);
    alert_label_ = lv_label_create(lv_screen_active());
    lv_obj_set_width(alert_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(alert_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(alert_label_, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_add_flag(alert_label_, LV_OBJ_FLAG_HIDDEN);

    reaction_emoji_ = lv_image_create(lv_screen_active());
    lv_obj_align(reaction_emoji_, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_add_flag(reaction_emoji_, LV_OBJ_FLAG_HIDDEN);
    if (reaction_emoji_timer_ == nullptr) {
        const esp_timer_create_args_t timer_args = {
            .callback = &MhaiBotDisplay::ReactionEmojiTimerCallback,
            .arg = this,
            .name = "mhaibot_reaction_emoji",
        };
        esp_timer_create(&timer_args, &reaction_emoji_timer_);
    }

    face_visible_ = true;
    ApplyFaceVisibility();
    ApplyEyesOnlyChrome();
    ESP_LOGI(TAG, "MhaiBot face initialized");
}

void MhaiBotDisplay::ShowReactionEmoji(const char* name, uint32_t duration_ms) {
    ESP_LOGI(TAG, "ShowReactionEmoji('%s') reaction_emoji_=%p current_theme_=%p", name,
             reaction_emoji_, current_theme_);
    if (reaction_emoji_ == nullptr || current_theme_ == nullptr) {
        return;
    }
    auto emoji_collection = static_cast<LvglTheme*>(current_theme_)->emoji_collection();
    ESP_LOGI(TAG, "ShowReactionEmoji emoji_collection=%p", emoji_collection.get());
    const LvglImage* image = emoji_collection != nullptr ? emoji_collection->GetEmojiImage(name) : nullptr;
    ESP_LOGI(TAG, "ShowReactionEmoji image=%p", image);
    if (image == nullptr) {
        return;
    }
    {
        DisplayLockGuard lock(this);
        lv_image_set_src(reaction_emoji_, image->image_dsc());
        lv_obj_remove_flag(reaction_emoji_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(reaction_emoji_);
    }
    if (reaction_emoji_timer_ != nullptr) {
        esp_timer_stop(reaction_emoji_timer_);
        esp_timer_start_once(reaction_emoji_timer_, static_cast<uint64_t>(duration_ms) * 1000);
    }
}

void MhaiBotDisplay::HideReactionEmoji() {
    if (reaction_emoji_timer_ != nullptr) {
        esp_timer_stop(reaction_emoji_timer_);
    }
    DisplayLockGuard lock(this);
    if (reaction_emoji_ != nullptr && lv_obj_is_valid(reaction_emoji_)) {
        lv_obj_add_flag(reaction_emoji_, LV_OBJ_FLAG_HIDDEN);
    }
}

void MhaiBotDisplay::ReactionEmojiTimerCallback(void* arg) {
    static_cast<MhaiBotDisplay*>(arg)->HideReactionEmoji();
}

void MhaiBotDisplay::SetEmotion(const char* emotion) {
    LogEmotionTransition(emotion, ToFaceEmotion(emotion));
    const bool is_error =
        pending_error_status_ && (IsErrorEmotion(emotion) || IsWarningEmotion(emotion));

    if (face_ == nullptr) {
        SpiLcdDisplay::SetEmotion(emotion);
        DisplayLockGuard lock(this);
        SetAlertState(is_error, battery_low_);
        pending_error_status_ = false;
        ApplyEyesOnlyChrome();
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
            SetAlertState(is_error, battery_low_);
            pending_error_status_ = false;
        }
        SpiLcdDisplay::SetEmotion(emotion);
        DisplayLockGuard lock(this);
        ApplyEyesOnlyChrome();
        return;
    }

    LogUnknownEmotionOnce(emotion);
    const bool force_listening_pose =
        listening_status_active_ && emotion != nullptr && strcmp(emotion, "neutral") == 0;

    DisplayLockGuard lock(this);
    face_visible_ = true;
    SetAlertState(is_error, battery_low_);
    pending_error_status_ = false;
    face_->SetEmotion(force_listening_pose ? MhaiBotFaceV2::Emotion::kListening : ToFaceEmotion(emotion));
    // Stop any GIF before showing the face so LVGL does not keep animating
    // a hidden emoji image.
    if (gif_controller_) {
        gif_controller_->Stop();
        gif_controller_.reset();
    }
    ApplyFaceVisibility();
    ApplyEyesOnlyChrome();
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
        ApplyEyesOnlyChrome();
    }
}

void MhaiBotDisplay::SetTheme(Theme* theme) {
    SpiLcdDisplay::SetTheme(theme);

    DisplayLockGuard lock(this);
    if (face_ == nullptr || theme == nullptr) {
        return;
    }
    face_->SetColor(lv_color_hex(kMhaiBotEyeColor));
    ApplyEyesOnlyChrome();
}

void MhaiBotDisplay::SetStatus(const char* status) {
    DisplayLockGuard lock(this);
    pending_error_status_ = IsErrorStatus(status);
    listening_status_active_ = status != nullptr && strcmp(status, Lang::Strings::LISTENING) == 0;
    if (!pending_error_status_) {
        SetAlertState(false, battery_low_);
    }
    ApplyEyesOnlyChrome();
    ApplyFaceVisibility();
}

void MhaiBotDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void MhaiBotDisplay::ShowNotification(const char* notification, int duration_ms) {
    (void)notification;
    (void)duration_ms;

    DisplayLockGuard lock(this);
    if (face_ != nullptr) {
        face_visible_ = true;
        face_->SetEmotion(MhaiBotFaceV2::Emotion::kHappy);
    }
    ApplyEyesOnlyChrome();
    ApplyFaceVisibility();
}

void MhaiBotDisplay::SetChatMessage(const char* role, const char* content) {
    (void)role;
    (void)content;

    DisplayLockGuard lock(this);
    ApplyEyesOnlyChrome();
    ApplyFaceVisibility();
}

void MhaiBotDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    ApplyEyesOnlyChrome();
    ApplyFaceVisibility();
}

void MhaiBotDisplay::UpdateStatusBar(bool update_all) {
    (void)update_all;

    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    bool low_battery = false;
    if (Board::GetInstance().GetBatteryLevel(battery_level, charging, discharging)) {
        if (!charging) {
            const int level_index =
                battery_level <= 0
                    ? 0
                    : (battery_level >= 100 ? 7 : 1 + ((battery_level - 1) * 6 / 99));
            low_battery = discharging && level_index == 0;
        }
    }

    DisplayLockGuard lock(this);
    SetAlertState(error_active_, low_battery);
    ApplyEyesOnlyChrome();
    ApplyFaceVisibility();
}

bool MhaiBotDisplay::SetPanelPowered(bool powered) {
    esp_err_t err = esp_lcd_panel_disp_on_off(panel_, powered);
    if (err == ESP_OK) {
        return true;
    }
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Panel on/off is not supported; using backlight only");
        return false;
    }
    ESP_LOGE(TAG, "Panel on/off failed: %s", esp_err_to_name(err));
    return false;
}

void MhaiBotDisplay::StartPetting() {
    {
        DisplayLockGuard lock(this);
        if (face_ != nullptr) {
            face_->StartPetting();
        }
    }
    ShowReactionEmoji("loving", MhaiBotPetDurationMs());
}

void MhaiBotDisplay::StartGroggyWake() {
    {
        DisplayLockGuard lock(this);
        if (face_ != nullptr) {
            face_->StartGroggyWake();
        }
    }
    ShowReactionEmoji("sleepy", MhaiBotGroggyWakeDurationMs());
}

void MhaiBotDisplay::StartStartled() {
    {
        DisplayLockGuard lock(this);
        if (face_ != nullptr) {
            face_->StartStartled();
        }
    }
    ShowReactionEmoji("shocked", MhaiBotStartleDurationMs());
}

void MhaiBotDisplay::CancelTransientAnimation() {
    {
        DisplayLockGuard lock(this);
        if (face_ != nullptr) {
            face_->CancelTransientAnimation();
        }
    }
    HideReactionEmoji();
}

bool MhaiBotDisplay::IsGroggyWakeActive() const {
    return face_ != nullptr && face_->IsGroggyWakeActive();
}
