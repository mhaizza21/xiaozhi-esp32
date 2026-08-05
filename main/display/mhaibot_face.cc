#include "mhaibot_face.h"

#include <esp_log.h>
#include <esp_random.h>

#define TAG "MhaiBotFace"

namespace {
// lv_arc defaults to draggable (LV_OBJ_FLAG_CLICKABLE) with a visible knob
// and background track; a face arc must be none of those things, or a
// stray touch near the face would let LVGL's own press handler silently
// overwrite our programmatic geometry, and a default-themed knob/track
// would paint stray artifacts at the arc's ends.
void ConfigureFaceArc(lv_obj_t* arc, lv_color_t color, int arc_width) {
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(arc, 0, 0);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc, arc_width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
}
}  // namespace

MhaiBotFace::MhaiBotFace(lv_obj_t* parent, lv_color_t eye_color)
    : MhaiBotFace(parent, eye_color, Config()) {}

MhaiBotFace::MhaiBotFace(lv_obj_t* parent, lv_color_t eye_color, const Config& config)
    : config_(config) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "parent is null, face not created");
        return;
    }
    CreateFace(parent, eye_color);
    if (root_ != nullptr) {
        StartBlinkTimer();
        StartSpeakingPulseTimer();
        Hide();
    }
}

MhaiBotFace::~MhaiBotFace() {
    destroying_ = true;
    StopBlinkTimer();
    StopSpeakingPulseTimer();
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        // Detach delete callback before deleting so it does not race teardown.
        lv_obj_remove_event_cb(root_, OnRootDeleted);
        lv_obj_del(root_);
    }
    root_ = nullptr;
    eye_row_ = nullptr;
    left_eye_ = nullptr;
    right_eye_ = nullptr;
    mouth_ = nullptr;
}

lv_obj_t* MhaiBotFace::CreateEyeArc(lv_obj_t* parent, lv_color_t eye_color) {
    lv_obj_t* eye = lv_arc_create(parent);
    const int box = config_.eye_radius_px * 2;
    lv_obj_set_size(eye, box, box);
    ConfigureFaceArc(eye, eye_color, config_.eye_arc_width);
    return eye;
}

void MhaiBotFace::CreateFace(lv_obj_t* parent, lv_color_t eye_color) {
    base_x_offset_ = config_.canvas_margin_x;

    const int neutral_pair_span = config_.eye_radius_px * 2 * 2 + config_.eye_gap;
    const int listening_pair_span =
        config_.listening_eye_radius * 2 * 2 + config_.listening_eye_gap;
    const int pair_span =
        neutral_pair_span > listening_pair_span ? neutral_pair_span : listening_pair_span;
    const int root_width = pair_span + config_.canvas_margin_x * 2;

    const int eye_box = config_.eye_radius_px > config_.listening_eye_radius
                            ? config_.eye_radius_px * 2
                            : config_.listening_eye_radius * 2;
    const int root_height =
        eye_box + config_.mouth_gap + config_.mouth_radius * 2 + config_.canvas_margin_y * 2;

    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, root_width, root_height);
    lv_obj_set_style_bg_opa(root_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(root_, LV_ALIGN_CENTER, 0, config_.vertical_offset);
    lv_obj_add_event_cb(root_, OnRootDeleted, LV_EVENT_DELETE, this);

    // eye_row_ isolates the eyes' horizontal-centering math from the
    // mouth's height below, so growing root_ to fit the mouth never
    // shifts the eyes' apparent position.
    eye_row_ = lv_obj_create(root_);
    lv_obj_set_size(eye_row_, root_width, eye_box);
    lv_obj_set_style_bg_opa(eye_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(eye_row_, 0, 0);
    lv_obj_set_style_pad_all(eye_row_, 0, 0);
    lv_obj_set_style_radius(eye_row_, 0, 0);
    lv_obj_clear_flag(eye_row_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(eye_row_, LV_ALIGN_TOP_MID, 0, config_.canvas_margin_y);

    left_eye_ = CreateEyeArc(eye_row_, eye_color);
    right_eye_ = CreateEyeArc(eye_row_, eye_color);
    ApplyGeometry(GetOpenGeometryForEmotion(emotion_));

    mouth_ = lv_arc_create(root_);
    const int mouth_box = config_.mouth_radius * 2;
    lv_obj_set_size(mouth_, mouth_box, mouth_box);
    ConfigureFaceArc(mouth_, eye_color, config_.mouth_arc_width);
    lv_obj_align(mouth_, LV_ALIGN_TOP_MID, 0,
                 config_.canvas_margin_y + eye_box + config_.mouth_gap);
    ApplyMouthGeometry(emotion_);
}

void MhaiBotFace::Show() {
    if (root_ == nullptr || !lv_obj_is_valid(root_)) {
        return;
    }
    // Always reopen eyes so a blink interrupted by Hide() does not leave the
    // face stuck in the Closed phase when it returns.
    OpenEyes();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    visible_ = true;
    if (blink_timer_ != nullptr) {
        const uint32_t open_ms = RandomIntervalMs(config_.blink_min_ms, config_.blink_max_ms);
        lv_timer_set_period(blink_timer_, open_ms);
        lv_timer_reset(blink_timer_);
        lv_timer_resume(blink_timer_);
    }

    pulse_phase_ = PulsePhase::Up;
    if (speaking_pulse_timer_ != nullptr) {
        if (emotion_ == Emotion::kSpeaking) {
            lv_timer_set_period(speaking_pulse_timer_, config_.speaking_pulse_period_ms / 2);
            lv_timer_reset(speaking_pulse_timer_);
            lv_timer_resume(speaking_pulse_timer_);
        } else {
            lv_timer_pause(speaking_pulse_timer_);
        }
    }
}

void MhaiBotFace::Hide() {
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    visible_ = false;
    if (blink_timer_ != nullptr) {
        lv_timer_pause(blink_timer_);
    }
    if (speaking_pulse_timer_ != nullptr) {
        lv_timer_pause(speaking_pulse_timer_);
    }
}

void MhaiBotFace::SetEmotion(Emotion emotion) {
    if (emotion == emotion_) {
        return;
    }
    const Emotion previous = emotion_;
    emotion_ = emotion;

    if (emotion_ == Emotion::kThinking) {
        // Roll a glance direction once per entry into Thinking, not per
        // blink, so repeated blinks while thinking don't swap sides.
        thinking_glance_dir_ = (esp_random() & 1) ? 1 : -1;
    }

    // Re-derive and fully overwrite geometry now, regardless of blink phase,
    // so emotion switches apply instantly instead of waiting for the next
    // blink tick, and rapid switching can never leave stale/partial geometry.
    if (phase_ == BlinkPhase::Open) {
        OpenEyes();
    } else {
        CloseEyes();
    }
    ApplyMouthGeometry(emotion_);

    if (previous == Emotion::kSpeaking && emotion_ != Emotion::kSpeaking) {
        pulse_phase_ = PulsePhase::Up;
        if (speaking_pulse_timer_ != nullptr) {
            lv_timer_pause(speaking_pulse_timer_);
        }
    } else if (emotion_ == Emotion::kSpeaking && previous != Emotion::kSpeaking) {
        pulse_phase_ = PulsePhase::Up;
        if (speaking_pulse_timer_ != nullptr) {
            lv_timer_set_period(speaking_pulse_timer_, config_.speaking_pulse_period_ms / 2);
            lv_timer_reset(speaking_pulse_timer_);
            if (visible_) {
                lv_timer_resume(speaking_pulse_timer_);
            }
        }
    }
}

void MhaiBotFace::SetColor(lv_color_t eye_color) {
    if (left_eye_ != nullptr && lv_obj_is_valid(left_eye_)) {
        lv_obj_set_style_arc_color(left_eye_, eye_color, LV_PART_INDICATOR);
    }
    if (right_eye_ != nullptr && lv_obj_is_valid(right_eye_)) {
        lv_obj_set_style_arc_color(right_eye_, eye_color, LV_PART_INDICATOR);
    }
    if (mouth_ != nullptr && lv_obj_is_valid(mouth_)) {
        lv_obj_set_style_arc_color(mouth_, eye_color, LV_PART_INDICATOR);
    }
}

void MhaiBotFace::StartBlinkTimer() {
    if (blink_timer_ != nullptr || root_ == nullptr) {
        return;
    }
    const uint32_t first_delay = RandomIntervalMs(config_.blink_min_ms, config_.blink_max_ms);
    blink_timer_ = lv_timer_create(BlinkTimerCb, first_delay, this);
    if (blink_timer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create blink timer");
        return;
    }
    // Start paused until Show(); Hide() was already called from the ctor.
    lv_timer_pause(blink_timer_);
}

void MhaiBotFace::StopBlinkTimer() {
    if (blink_timer_ == nullptr) {
        return;
    }
    lv_timer_delete(blink_timer_);
    blink_timer_ = nullptr;
}

void MhaiBotFace::StartSpeakingPulseTimer() {
    if (speaking_pulse_timer_ != nullptr || root_ == nullptr) {
        return;
    }
    speaking_pulse_timer_ =
        lv_timer_create(SpeakingPulseTimerCb, config_.speaking_pulse_period_ms / 2, this);
    if (speaking_pulse_timer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create speaking pulse timer");
        return;
    }
    // Only ever runs while emotion_ == kSpeaking; starts paused like blink_timer_.
    lv_timer_pause(speaking_pulse_timer_);
}

void MhaiBotFace::StopSpeakingPulseTimer() {
    if (speaking_pulse_timer_ == nullptr) {
        return;
    }
    lv_timer_delete(speaking_pulse_timer_);
    speaking_pulse_timer_ = nullptr;
}

void MhaiBotFace::ApplyGeometry(const EyeGeometry& geometry) {
    if (left_eye_ == nullptr || right_eye_ == nullptr || !lv_obj_is_valid(left_eye_) ||
        !lv_obj_is_valid(right_eye_)) {
        return;
    }
    const int box = geometry.radius * 2;
    lv_obj_set_size(left_eye_, box, box);
    lv_obj_set_size(right_eye_, box, box);
    lv_arc_set_angles(left_eye_, 270 - geometry.span_deg / 2, 270 + geometry.span_deg / 2);
    lv_arc_set_angles(right_eye_, 270 - geometry.span_deg / 2, 270 + geometry.span_deg / 2);
    lv_obj_align(left_eye_, LV_ALIGN_LEFT_MID, geometry.x_left, 0);
    lv_obj_align(right_eye_, LV_ALIGN_LEFT_MID, geometry.x_right, 0);
}

MhaiBotFace::EyeGeometry MhaiBotFace::GetOpenGeometryForEmotion(Emotion emotion) const {
    switch (emotion) {
        case Emotion::kHappy:
            // Narrower span than neutral; may need visual refinement once a
            // distinct Sleepy state is added later, since both are
            // "half-closed" eyes.
            return {config_.happy_eye_span_deg, config_.eye_radius_px, base_x_offset_,
                    base_x_offset_ + config_.eye_radius_px * 2 + config_.eye_gap};
        case Emotion::kThinking: {
            const int shift = thinking_glance_dir_ * config_.thinking_glance_offset;
            return {config_.thinking_eye_span_deg, config_.eye_radius_px, base_x_offset_ + shift,
                    base_x_offset_ + config_.eye_radius_px * 2 + config_.eye_gap + shift};
        }
        case Emotion::kListening:
            return {config_.listening_eye_span_deg, config_.listening_eye_radius, base_x_offset_,
                    base_x_offset_ + config_.listening_eye_radius * 2 + config_.listening_eye_gap};
        default:
            // Neutral baseline; also used by kSpeaking (pulse timer applies a
            // small span delta on top) and by kRobot2/kRelaxed/kConfident,
            // which have no dedicated geometry yet.
            return {config_.eye_open_span_deg, config_.eye_radius_px, base_x_offset_,
                    base_x_offset_ + config_.eye_radius_px * 2 + config_.eye_gap};
    }
}

int MhaiBotFace::GetMouthHalfSpanForEmotion(Emotion emotion) const {
    switch (emotion) {
        case Emotion::kHappy:
            return config_.mouth_happy_half_span_deg;
        default:
            // Flat/neutral resting mouth for every other emotion, including
            // unmapped ones — only Happy should read as smiling.
            return config_.mouth_neutral_half_span_deg;
    }
}

void MhaiBotFace::ApplyMouthGeometry(Emotion emotion) {
    if (mouth_ == nullptr || !lv_obj_is_valid(mouth_)) {
        return;
    }
    const int half_span = GetMouthHalfSpanForEmotion(emotion);
    lv_arc_set_angles(mouth_, 90 - half_span, 90 + half_span);
}

void MhaiBotFace::OpenEyes() {
    if (left_eye_ == nullptr || right_eye_ == nullptr || !lv_obj_is_valid(left_eye_) ||
        !lv_obj_is_valid(right_eye_)) {
        return;
    }
    ApplyGeometry(GetOpenGeometryForEmotion(emotion_));
    phase_ = BlinkPhase::Open;
}

void MhaiBotFace::CloseEyes() {
    if (left_eye_ == nullptr || right_eye_ == nullptr || !lv_obj_is_valid(left_eye_) ||
        !lv_obj_is_valid(right_eye_)) {
        return;
    }
    // Keep the current emotion's horizontal footprint (glance offset /
    // widened layout) while collapsing the open span, so blinking
    // mid-emotion does not snap back to neutral and then re-glance on
    // reopen.
    EyeGeometry geometry = GetOpenGeometryForEmotion(emotion_);
    geometry.span_deg = config_.eye_closed_span_deg;
    ApplyGeometry(geometry);
    phase_ = BlinkPhase::Closed;
}

uint32_t MhaiBotFace::RandomIntervalMs(uint32_t min_ms, uint32_t max_ms) const {
    if (max_ms <= min_ms) {
        return min_ms;
    }
    const uint32_t span = max_ms - min_ms + 1;
    return min_ms + (esp_random() % span);
}

void MhaiBotFace::BlinkTimerCb(lv_timer_t* timer) {
    auto* self = static_cast<MhaiBotFace*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->destroying_ || !self->visible_) {
        return;
    }
    if (self->root_ == nullptr || !lv_obj_is_valid(self->root_) || self->left_eye_ == nullptr ||
        self->right_eye_ == nullptr || !lv_obj_is_valid(self->left_eye_) ||
        !lv_obj_is_valid(self->right_eye_)) {
        return;
    }

    if (self->phase_ == BlinkPhase::Open) {
        self->CloseEyes();
        const uint32_t close_ms = self->RandomIntervalMs(self->config_.blink_close_min_ms,
                                                         self->config_.blink_close_max_ms);
        lv_timer_set_period(timer, close_ms);
        lv_timer_reset(timer);
    } else {
        self->OpenEyes();
        const uint32_t open_ms =
            self->RandomIntervalMs(self->config_.blink_min_ms, self->config_.blink_max_ms);
        lv_timer_set_period(timer, open_ms);
        lv_timer_reset(timer);
    }
}

void MhaiBotFace::SpeakingPulseTimerCb(lv_timer_t* timer) {
    auto* self = static_cast<MhaiBotFace*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->destroying_ || !self->visible_ ||
        self->emotion_ != Emotion::kSpeaking || self->phase_ == BlinkPhase::Closed) {
        // No-op while mid-blink: the next blink boundary unconditionally
        // overwrites geometry, so the pulse always resumes from a known-good
        // baseline with no drift.
        return;
    }
    if (self->left_eye_ == nullptr || self->right_eye_ == nullptr ||
        !lv_obj_is_valid(self->left_eye_) || !lv_obj_is_valid(self->right_eye_)) {
        return;
    }

    self->pulse_phase_ = self->pulse_phase_ == PulsePhase::Up ? PulsePhase::Down : PulsePhase::Up;
    EyeGeometry geometry = self->GetOpenGeometryForEmotion(Emotion::kSpeaking);
    if (self->pulse_phase_ == PulsePhase::Down) {
        geometry.span_deg -= self->config_.speaking_pulse_span_delta_deg;
    }
    self->ApplyGeometry(geometry);
}

void MhaiBotFace::OnRootDeleted(lv_event_t* event) {
    auto* self = static_cast<MhaiBotFace*>(lv_event_get_user_data(event));
    if (self == nullptr || self->destroying_) {
        return;
    }
    // LVGL is deleting the root (or an ancestor). Stop the timers and null
    // pointers only — do not delete root_ again (would recurse).
    self->StopBlinkTimer();
    self->StopSpeakingPulseTimer();
    self->root_ = nullptr;
    self->eye_row_ = nullptr;
    self->left_eye_ = nullptr;
    self->right_eye_ = nullptr;
    self->mouth_ = nullptr;
    self->visible_ = false;
}
