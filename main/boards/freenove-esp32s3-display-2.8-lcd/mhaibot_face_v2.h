#ifndef MHAIBOT_FACE_V2_H
#define MHAIBOT_FACE_V2_H

#include "eye/eye_intent_mailbox.h"
#include "eye/lvgl_eye_renderer.h"

#include <lvgl.h>

#include <cstdint>

class MhaiBotFaceV2 {
public:
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

    MhaiBotFaceV2(lv_obj_t* parent, lv_color_t eye_color);
    MhaiBotFaceV2(lv_obj_t* parent, lv_color_t eye_color, const Config& config);
    ~MhaiBotFaceV2();

    MhaiBotFaceV2(const MhaiBotFaceV2&) = delete;
    MhaiBotFaceV2& operator=(const MhaiBotFaceV2&) = delete;

    void Show();
    void Hide();
    void SetColor(lv_color_t eye_color);
    void SetEmotion(Emotion emotion);
    void StartPetting();
    void StartGroggyWake();
    void StartStartled();
    void CancelTransientAnimation();

    bool IsVisible() const { return visible_; }
    bool IsGroggyWakeActive() const;
    Emotion GetEmotion() const { return target_emotion_; }

private:
    enum class TransientMode { kNone, kPetting, kGroggyWake, kStartled };

    struct Pose {
        int left_x;
        int right_x;
        int y;
        int width;
        int height;
        int radius;
    };

    void CreateFace(lv_obj_t* parent, lv_color_t eye_color);
    lv_obj_t* CreateEye(lv_obj_t* parent, lv_color_t eye_color);
    void StartTimer();
    void StopTimer();
    void Tick(uint32_t elapsed_ms);
    void ApplyPose(const Pose& pose, lv_opa_t opa);
    void ApplySleepLabel(uint32_t elapsed_ms);
    void HideSleepLabel();
    void BeginTransitionTo(Emotion emotion);
    Pose ResolveBasePose(Emotion emotion) const;
    Pose ResolveRenderedPose() const;
    Pose InterpolatePose(const Pose& from, const Pose& to, uint16_t progress_per_mille) const;
    Pose PettingPose() const;
    Pose GroggyPose(uint32_t elapsed_ms) const;
    Pose StartledPose() const;
    static uint16_t ClampProgress(uint32_t elapsed_ms, uint32_t duration_ms);
    static int LerpInt(int from, int to, uint16_t progress_per_mille);
    static void TickTimerCb(lv_timer_t* timer);
    static void OnRootDeleted(lv_event_t* event);

    Config config_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* sleep_label_ = nullptr;
    lv_timer_t* tick_timer_ = nullptr;
    LVGLEyeRenderer renderer_;
    // Slice 2 (cutover A): mailbox exists and is consumed per tick, but
    // legacy ResolveRenderedPose remains sole pixel authority (09 §"Dual-
    // path mailbox / cutover sequence" step A). Nothing publishes yet.
    EyeIntentMailbox eye_intent_mailbox_;
    lv_color_t eye_color_;
    Pose current_pose_{};
    Pose transition_from_{};
    Emotion target_emotion_ = Emotion::kNeutral;
    TransientMode transient_mode_ = TransientMode::kNone;
    uint32_t transition_elapsed_ms_ = 0;
    uint32_t transient_elapsed_ms_ = 0;
    uint32_t sleep_elapsed_ms_ = 0;
    uint32_t last_tick_ms_ = 0;
    uint32_t frame_ = 0;
    bool visible_ = false;
    bool destroying_ = false;
};

#endif  // MHAIBOT_FACE_V2_H
