#ifndef MHAIBOT_DISPLAY_H
#define MHAIBOT_DISPLAY_H

#include "display/lcd_display.h"
#include "mhaibot_face_v2.h"

#include <memory>
#include <string>
#include <unordered_set>

/**
 * Freenove board-local LcdDisplay subclass that keeps the display in an
 * eyes-only MhaiBot face mode and routes status/notification/chat lifecycle
 * updates away from the inherited chrome.
 */
class MhaiBotDisplay : public SpiLcdDisplay {
public:
    MhaiBotDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                   int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                   bool swap_xy);
    ~MhaiBotDisplay() override;

    void SetupUI() override;
    void SetEmotion(const char* emotion) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetTheme(Theme* theme) override;
    void SetStatus(const char* status) override;
    void ShowNotification(const char* notification, int duration_ms = 3000) override;
    void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void UpdateStatusBar(bool update_all = false) override;
    bool SetPanelPowered(bool powered);
    void StartPetting();
    void StartGroggyWake();
    void CancelTransientAnimation();
    bool IsGroggyWakeActive() const;

private:
    static bool IsLegacyEmotion(const char* emotion);
    static MhaiBotFaceV2::Emotion ToFaceEmotion(const char* emotion);
    void LogUnknownEmotionOnce(const char* emotion);
    void LogEmotionTransition(const char* emotion, MhaiBotFaceV2::Emotion mapped);
    void ApplyFaceVisibility();
    void ApplyEyesOnlyChrome();
    void SetAlertState(bool error_active, bool battery_low);
    void UpdateAlertLabel();
    static bool IsErrorStatus(const char* status);
    static bool IsErrorEmotion(const char* emotion);
    static bool IsWarningEmotion(const char* emotion);
    static bool IsNotificationEmotion(const char* emotion);

    std::unique_ptr<MhaiBotFaceV2> face_;
    lv_obj_t* alert_label_ = nullptr;
    bool face_visible_ = true;
    bool error_active_ = false;
    bool battery_low_ = false;
    bool pending_error_status_ = false;
    std::unordered_set<std::string> logged_unknown_emotions_;
    std::string last_logged_emotion_;
    bool has_logged_emotion_ = false;
};

#endif  // MHAIBOT_DISPLAY_H
