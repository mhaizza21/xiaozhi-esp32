#ifndef MHAIBOT_DISPLAY_H
#define MHAIBOT_DISPLAY_H

#include "display/lcd_display.h"
#include "eye/eye_activity_adapter.h"
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
    void StartStartled();
    void CancelTransientAnimation();
    bool IsGroggyWakeActive() const;

    // Slice 11B: engineer-only passthrough to MhaiBotFaceV2::SetPixelSource/
    // GetPixelSource (Slice 11A), called only from the UART console command
    // registered in eye_pixel_source_console.cc. Not reachable via MCP,
    // voice, or any user-facing UI (09 Slice 11B).
    void SetEyePixelSource(MhaiBotFaceV2::PixelSource source);
    MhaiBotFaceV2::PixelSource GetEyePixelSource() const;

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
    // Briefly shows a color emoji badge (top-right) alongside a transient
    // face reaction (petting/startled/groggy-wake), so the reaction reads
    // clearly even though the normal UI is eyes-only. Auto-hides after
    // duration_ms via reaction_emoji_timer_.
    void ShowReactionEmoji(const char* name, uint32_t duration_ms);
    void HideReactionEmoji();
    static void ReactionEmojiTimerCallback(void* arg);

    std::unique_ptr<MhaiBotFaceV2> face_;
    // Slice 3 (dual-path step B start): shadow-publishes an EyeIntent in
    // parallel with face_->SetEmotion(...). Pure/stateless; does not affect
    // pixels (09 / ADR-002).
    EyeActivityAdapter eye_activity_adapter_;
    lv_obj_t* alert_label_ = nullptr;
    lv_obj_t* reaction_emoji_ = nullptr;
    esp_timer_handle_t reaction_emoji_timer_ = nullptr;
    bool face_visible_ = true;
    bool error_active_ = false;
    bool battery_low_ = false;
    bool pending_error_status_ = false;
    // Application::HandleStateChangedEvent() calls SetStatus(LISTENING)
    // immediately before SetEmotion("neutral") when entering the Listening
    // state, since core app code has no dedicated "listening" emotion call.
    // Remember that so SetEmotion() can show the Listening pose instead.
    bool listening_status_active_ = false;
    std::unordered_set<std::string> logged_unknown_emotions_;
    std::string last_logged_emotion_;
    bool has_logged_emotion_ = false;
};

#endif  // MHAIBOT_DISPLAY_H
