#ifndef MHAIBOT_FACE_H
#define MHAIBOT_FACE_H

#include <lvgl.h>

#include <cstdint>

/**
 * Minimal reusable MhaiBot face: two curved "^_^" eyes and a smile mouth,
 * built from LVGL arcs, with randomized blinking and per-emotion eye
 * geometry driven entirely by LVGL timers (no FreeRTOS task, no blocking
 * delay).
 */
class MhaiBotFace {
public:
    struct Config {
        int eye_radius_px = 34;
        int eye_open_span_deg = 100;
        int eye_closed_span_deg = 8;
        int eye_arc_width = 14;
        int eye_gap = 48;
        int vertical_offset = -8;
        uint32_t blink_min_ms = 2500;
        uint32_t blink_max_ms = 6000;
        uint32_t blink_close_min_ms = 100;
        uint32_t blink_close_max_ms = 180;

        // Invisible headroom added around root_ so the Thinking glance
        // shift, Listening's wider eyes, and the arc strokes' rounded-cap
        // overdraw are never clipped against root_'s bounds.
        int canvas_margin_x = 16;
        int canvas_margin_y = 10;

        int happy_eye_span_deg = 60;
        int thinking_eye_span_deg = 90;
        int thinking_glance_offset = 14;
        int listening_eye_radius = 42;
        int listening_eye_span_deg = 120;
        int listening_eye_gap = 16;
        uint32_t speaking_pulse_period_ms = 420;
        int speaking_pulse_span_delta_deg = 10;

        int mouth_radius = 46;
        int mouth_half_span_deg = 34;
        int mouth_arc_width = 10;
        int mouth_gap = 12;
    };

    // Emotions the face can represent. Neutral/Happy/Thinking/Listening/
    // Speaking each render distinct eye geometry; kRobot2/kRelaxed/
    // kConfident fall back to the neutral shape until a future version
    // defines them. The mouth stays a constant smile across all emotions.
    enum class Emotion {
        kNeutral,
        kRobot2,
        kHappy,
        kThinking,
        kSpeaking,
        kListening,
        kRelaxed,
        kConfident,
    };

    MhaiBotFace(lv_obj_t* parent, lv_color_t eye_color);
    MhaiBotFace(lv_obj_t* parent, lv_color_t eye_color, const Config& config);
    ~MhaiBotFace();

    MhaiBotFace(const MhaiBotFace&) = delete;
    MhaiBotFace& operator=(const MhaiBotFace&) = delete;

    void Show();
    void Hide();
    void SetColor(lv_color_t eye_color);
    void SetEmotion(Emotion emotion);

    bool IsVisible() const { return visible_; }
    Emotion GetEmotion() const { return emotion_; }

private:
    enum class BlinkPhase { Open, Closed };
    enum class PulsePhase { Up, Down };

    struct EyeGeometry {
        int span_deg;
        int radius;
        int x_left;
        int x_right;
    };

    void CreateFace(lv_obj_t* parent, lv_color_t eye_color);
    lv_obj_t* CreateEyeArc(lv_obj_t* parent, lv_color_t eye_color);
    void StartBlinkTimer();
    void StopBlinkTimer();
    void StartSpeakingPulseTimer();
    void StopSpeakingPulseTimer();
    void OpenEyes();
    void CloseEyes();
    void ApplyGeometry(const EyeGeometry& geometry);
    EyeGeometry GetOpenGeometryForEmotion(Emotion emotion) const;
    uint32_t RandomIntervalMs(uint32_t min_ms, uint32_t max_ms) const;
    static void BlinkTimerCb(lv_timer_t* timer);
    static void SpeakingPulseTimerCb(lv_timer_t* timer);
    static void OnRootDeleted(lv_event_t* event);

    Config config_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* eye_row_ = nullptr;
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* mouth_ = nullptr;
    lv_timer_t* blink_timer_ = nullptr;
    lv_timer_t* speaking_pulse_timer_ = nullptr;
    int base_x_offset_ = 0;
    BlinkPhase phase_ = BlinkPhase::Open;
    PulsePhase pulse_phase_ = PulsePhase::Up;
    Emotion emotion_ = Emotion::kNeutral;
    int thinking_glance_dir_ = 1;
    bool visible_ = false;
    bool destroying_ = false;
};

#endif  // MHAIBOT_FACE_H
