#include "mhaibot_face_v2.h"

#include "eye/eye_pose_adapter.h"
#include "mhaibot_interaction_model.h"

#include <esp_log.h>

#include <cassert>
#include <string_view>

#define TAG "MhaiBotFaceV2"

namespace {
constexpr uint32_t kBackgroundColor = 0x000000;
constexpr uint8_t kSleepLabelOpa = LV_OPA_40 + 13;
}  // namespace

MhaiBotFaceV2::MhaiBotFaceV2(lv_obj_t* parent, lv_color_t eye_color)
    : MhaiBotFaceV2(parent, eye_color, Config()) {}

MhaiBotFaceV2::MhaiBotFaceV2(lv_obj_t* parent, lv_color_t eye_color, const Config& config)
    : config_(config), eye_color_(eye_color) {
    if (parent == nullptr) {
        ESP_LOGE(TAG, "parent is null, face not created");
        return;
    }
    current_pose_ = ResolveBasePose(target_emotion_);
    transition_from_ = current_pose_;
    CreateFace(parent, eye_color);
    if (root_ != nullptr) {
        StartTimer();
        Hide();
    }
}

MhaiBotFaceV2::~MhaiBotFaceV2() {
    destroying_ = true;
    StopTimer();
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_remove_event_cb(root_, OnRootDeleted);
        lv_obj_del(root_);
    }
    renderer_.Deinit();
    root_ = nullptr;
    left_eye_ = nullptr;
    right_eye_ = nullptr;
    sleep_label_ = nullptr;
}

lv_obj_t* MhaiBotFaceV2::CreateEye(lv_obj_t* parent, lv_color_t eye_color) {
    lv_obj_t* eye = lv_obj_create(parent);
    lv_obj_set_style_bg_color(eye, eye_color, 0);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(eye, 0, 0);
    lv_obj_set_style_pad_all(eye, 0, 0);
    lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(eye, LV_OBJ_FLAG_CLICKABLE);
    return eye;
}

void MhaiBotFaceV2::CreateFace(lv_obj_t* parent, lv_color_t eye_color) {
    root_ = lv_obj_create(parent);
    lv_obj_set_size(root_, config_.root_width, config_.root_height);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBackgroundColor), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(root_, LV_ALIGN_CENTER, 0, config_.vertical_offset);
    lv_obj_add_event_cb(root_, OnRootDeleted, LV_EVENT_DELETE, this);

    left_eye_ = CreateEye(root_, eye_color);
    right_eye_ = CreateEye(root_, eye_color);
    renderer_.Init(left_eye_, right_eye_);

    sleep_label_ = lv_label_create(root_);
    lv_obj_set_style_text_color(sleep_label_, eye_color, 0);
    lv_obj_set_style_text_opa(sleep_label_, kSleepLabelOpa, 0);
    lv_obj_set_style_bg_opa(sleep_label_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(sleep_label_, 0, 0);
    lv_label_set_text_static(sleep_label_, "");
    lv_obj_align(sleep_label_, LV_ALIGN_TOP_RIGHT, -40, 22);
    lv_obj_add_flag(sleep_label_, LV_OBJ_FLAG_HIDDEN);

    ApplyPose(current_pose_, LV_OPA_COVER);
}

void MhaiBotFaceV2::StartTimer() {
    if (tick_timer_ != nullptr || root_ == nullptr) {
        return;
    }
    last_tick_ms_ = lv_tick_get();
    tick_timer_ = lv_timer_create(TickTimerCb, config_.tick_ms, this);
    if (tick_timer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create face timer");
        return;
    }
    lv_timer_pause(tick_timer_);
}

void MhaiBotFaceV2::StopTimer() {
    if (tick_timer_ == nullptr) {
        return;
    }
    lv_timer_delete(tick_timer_);
    tick_timer_ = nullptr;
}

void MhaiBotFaceV2::Show() {
    if (root_ == nullptr || !lv_obj_is_valid(root_)) {
        return;
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    visible_ = true;
    last_tick_ms_ = lv_tick_get();
    if (tick_timer_ != nullptr) {
        lv_timer_reset(tick_timer_);
        lv_timer_resume(tick_timer_);
    }
    Tick(0);
}

void MhaiBotFaceV2::Hide() {
    if (root_ != nullptr && lv_obj_is_valid(root_)) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
    visible_ = false;
    if (tick_timer_ != nullptr) {
        lv_timer_pause(tick_timer_);
    }
}

void MhaiBotFaceV2::SetColor(lv_color_t eye_color) {
    eye_color_ = eye_color;
    if (left_eye_ != nullptr && lv_obj_is_valid(left_eye_)) {
        lv_obj_set_style_bg_color(left_eye_, eye_color, 0);
    }
    if (right_eye_ != nullptr && lv_obj_is_valid(right_eye_)) {
        lv_obj_set_style_bg_color(right_eye_, eye_color, 0);
    }
    if (sleep_label_ != nullptr && lv_obj_is_valid(sleep_label_)) {
        lv_obj_set_style_text_color(sleep_label_, eye_color, 0);
    }
}

void MhaiBotFaceV2::SetEmotion(Emotion emotion) {
    CancelTransientAnimation();
    BeginTransitionTo(emotion);
}

void MhaiBotFaceV2::StartPetting() {
    transient_mode_ = TransientMode::kPetting;
    transient_elapsed_ms_ = 0;
    HideSleepLabel();
}

void MhaiBotFaceV2::StartStartled() {
    transient_mode_ = TransientMode::kStartled;
    transient_elapsed_ms_ = 0;
    HideSleepLabel();
}

void MhaiBotFaceV2::StartGroggyWake() {
    if (transient_mode_ == TransientMode::kGroggyWake) {
        return;
    }
    transient_mode_ = TransientMode::kGroggyWake;
    transient_elapsed_ms_ = 0;
    HideSleepLabel();
}

void MhaiBotFaceV2::CancelTransientAnimation() {
    if (transient_mode_ == TransientMode::kNone) {
        return;
    }
    transient_mode_ = TransientMode::kNone;
    transient_elapsed_ms_ = 0;
    HideSleepLabel();
}

bool MhaiBotFaceV2::IsGroggyWakeActive() const {
    return transient_mode_ == TransientMode::kGroggyWake;
}

void MhaiBotFaceV2::PublishIntent(const EyeIntent& intent) {
    eye_intent_mailbox_.Publish(intent);
}

void MhaiBotFaceV2::BeginTransitionTo(Emotion emotion) {
    transition_from_ = current_pose_;
    transition_elapsed_ms_ = 0;
    target_emotion_ = emotion;
    if (target_emotion_ != Emotion::kSleeping) {
        sleep_elapsed_ms_ = 0;
        HideSleepLabel();
    }
}

void MhaiBotFaceV2::Tick(uint32_t elapsed_ms) {
    ++frame_;

    // Slice 2: consume the mailbox each tick so it is exercised from a real
    // LVGL-safe context, but nothing publishes yet and legacy pose
    // resolution remains pixel authority until Slice 11.
    EyeIntent mailbox_intent{};
    if (eye_intent_mailbox_.ConsumeLatest(&mailbox_intent)) {
        (void)mailbox_intent;
    }

    if (transition_elapsed_ms_ < config_.transition_ms) {
        transition_elapsed_ms_ += elapsed_ms;
    }

    if (transient_mode_ != TransientMode::kNone) {
        transient_elapsed_ms_ += elapsed_ms;
    }

    current_pose_ = ResolveRenderedPose();

    lv_opa_t opa = LV_OPA_COVER;
    if (transient_mode_ == TransientMode::kPetting) {
        // Slow triangle-wave shimmer (0..4..0 every ~1.1s) so the closed,
        // content eyes gently glow rather than sitting at flat opacity.
        const int glow_phase = static_cast<int>((frame_ / 4) % 8);
        const int glow_step = glow_phase <= 4 ? glow_phase : 8 - glow_phase;
        opa = static_cast<lv_opa_t>(200 + glow_step * 13);
    }
    ApplyPose(current_pose_, opa);

    if (transient_mode_ == TransientMode::kPetting &&
        transient_elapsed_ms_ >= MhaiBotPetDurationMs()) {
        transient_mode_ = TransientMode::kNone;
        transient_elapsed_ms_ = 0;
        BeginTransitionTo(target_emotion_);
    } else if (transient_mode_ == TransientMode::kGroggyWake &&
               transient_elapsed_ms_ >= MhaiBotGroggyWakeDurationMs()) {
        transient_mode_ = TransientMode::kNone;
        transient_elapsed_ms_ = 0;
        BeginTransitionTo(Emotion::kNeutral);
    } else if (transient_mode_ == TransientMode::kStartled &&
               transient_elapsed_ms_ >= MhaiBotStartleDurationMs()) {
        transient_mode_ = TransientMode::kNone;
        transient_elapsed_ms_ = 0;
        BeginTransitionTo(target_emotion_);
    }

    if (transient_mode_ == TransientMode::kNone && target_emotion_ == Emotion::kSleeping) {
        sleep_elapsed_ms_ += elapsed_ms;
        ApplySleepLabel(sleep_elapsed_ms_);
    } else {
        HideSleepLabel();
    }
}

void MhaiBotFaceV2::ApplyPose(const Pose& pose, lv_opa_t opa) {
    if (left_eye_ == nullptr || right_eye_ == nullptr || !lv_obj_is_valid(left_eye_) ||
        !lv_obj_is_valid(right_eye_)) {
        return;
    }

    // Slice 1: all left/right eye LVGL mutation goes through
    // LVGLEyeRenderer::Render (02 / 06 §4.5). ADR-003: sleep_label_ /
    // Show-Hide / backlight stay outside the renderer.
    const FaceV2Pose adapter_pose{pose.left_x, pose.right_x, pose.y,
                                  pose.width,  pose.height,  pose.radius};
    const float opacity = static_cast<float>(opa) / 255.0f;
    const EyeFrame frame = PoseToEyeFrame(adapter_pose, opacity);

#ifndef NDEBUG
    const FaceV2Pose roundtrip = EyeFrameToPose(frame);
    assert(roundtrip.left_x == pose.left_x && roundtrip.right_x == pose.right_x &&
           roundtrip.y == pose.y && roundtrip.width == pose.width &&
           roundtrip.height == pose.height && roundtrip.radius == pose.radius);
#endif

    renderer_.Render(frame);
}

void MhaiBotFaceV2::ApplySleepLabel(uint32_t elapsed_ms) {
    if (sleep_label_ == nullptr || !lv_obj_is_valid(sleep_label_)) {
        return;
    }
    const std::string_view text = MhaiBotSleepText(elapsed_ms);
    if (text.empty()) {
        lv_label_set_text_static(sleep_label_, "");
        lv_obj_add_flag(sleep_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text_static(sleep_label_, text.data());
        lv_obj_remove_flag(sleep_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(sleep_label_);
    }
}

void MhaiBotFaceV2::HideSleepLabel() {
    if (sleep_label_ != nullptr && lv_obj_is_valid(sleep_label_)) {
        lv_label_set_text_static(sleep_label_, "");
        lv_obj_add_flag(sleep_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

MhaiBotFaceV2::Pose MhaiBotFaceV2::ResolveBasePose(Emotion emotion) const {
    const int pair_width = config_.eye_width * 2 + config_.eye_gap;
    const int left = (config_.root_width - pair_width) / 2;
    const int right = left + config_.eye_width + config_.eye_gap;
    const Pose neutral{
        left, right, config_.eye_y, config_.eye_width, config_.eye_height, config_.corner_radius};

    switch (emotion) {
        case Emotion::kHappy:
            return {left, right, config_.eye_y + 8, config_.eye_width, 34, config_.corner_radius};
        case Emotion::kThinking:
            return {left - 8,          right - 8, config_.eye_y + 3,
                    config_.eye_width, 46,        config_.corner_radius};
        case Emotion::kSpeaking:
            return {left, right, config_.eye_y + 2, config_.eye_width, 54, config_.corner_radius};
        case Emotion::kListening:
            return {left - 5,
                    right + 5,
                    config_.eye_y - 4,
                    config_.eye_width + 10,
                    config_.eye_height + 8,
                    config_.corner_radius};
        case Emotion::kRelaxed:
        case Emotion::kSleepy:
            return {left, right, config_.eye_y + 13, config_.eye_width, 24, config_.corner_radius};
        case Emotion::kSleeping:
            return {left, right, config_.eye_y + 22, config_.eye_width, 8, config_.corner_radius};
        case Emotion::kConfident:
            return {left + 4,          right - 4, config_.eye_y + 2,
                    config_.eye_width, 48,        config_.corner_radius};
        case Emotion::kRobot2:
        case Emotion::kNeutral:
        default:
            return neutral;
    }
}

MhaiBotFaceV2::Pose MhaiBotFaceV2::ResolveRenderedPose() const {
    if (transient_mode_ == TransientMode::kPetting) {
        const uint16_t in_progress = ClampProgress(transient_elapsed_ms_, config_.transition_ms);
        Pose pose = InterpolatePose(current_pose_, PettingPose(), in_progress);
        const int sway = static_cast<int>((frame_ / 5) % 5) - 2;
        const int bob = static_cast<int>((frame_ / 8) % 3) - 1;
        pose.left_x += sway;
        pose.right_x += sway;
        pose.y += bob;
        return pose;
    }

    if (transient_mode_ == TransientMode::kGroggyWake) {
        return GroggyPose(transient_elapsed_ms_);
    }

    if (transient_mode_ == TransientMode::kStartled) {
        // Snap wide open immediately, then relax back to the current
        // emotion's pose over the rest of the startle window.
        const uint16_t progress = ClampProgress(transient_elapsed_ms_, MhaiBotStartleDurationMs());
        return InterpolatePose(StartledPose(), ResolveBasePose(target_emotion_), progress);
    }

    const Pose target = ResolveBasePose(target_emotion_);
    const uint16_t progress = ClampProgress(transition_elapsed_ms_, config_.transition_ms);
    return InterpolatePose(transition_from_, target, progress);
}

MhaiBotFaceV2::Pose MhaiBotFaceV2::InterpolatePose(const Pose& from, const Pose& to,
                                                   uint16_t progress_per_mille) const {
    return {
        LerpInt(from.left_x, to.left_x, progress_per_mille),
        LerpInt(from.right_x, to.right_x, progress_per_mille),
        LerpInt(from.y, to.y, progress_per_mille),
        LerpInt(from.width, to.width, progress_per_mille),
        LerpInt(from.height, to.height, progress_per_mille),
        LerpInt(from.radius, to.radius, progress_per_mille),
    };
}

MhaiBotFaceV2::Pose MhaiBotFaceV2::PettingPose() const {
    Pose pose = ResolveBasePose(Emotion::kHappy);
    // Thin, nearly-closed eyes read clearly as a content ^_^ squint (distinct
    // from the lighter kHappy squint and from the fully-closed Sleeping
    // pose); the near-circular radius keeps the ends soft/rounded.
    pose.height = 10;
    pose.radius = pose.height / 2;
    pose.y += 12;
    return pose;
}

MhaiBotFaceV2::Pose MhaiBotFaceV2::StartledPose() const {
    Pose pose = ResolveBasePose(target_emotion_);
    pose.left_x -= 7;
    pose.right_x += 7;
    pose.width += 14;
    pose.height += 20;
    pose.y -= 8;
    return pose;
}

MhaiBotFaceV2::Pose MhaiBotFaceV2::GroggyPose(uint32_t elapsed_ms) const {
    const uint16_t progress = MhaiBotGroggyProgressPerMille(elapsed_ms);
    const Pose sleeping = ResolveBasePose(Emotion::kSleeping);
    const Pose sleepy = ResolveBasePose(Emotion::kSleepy);
    const Pose neutral = ResolveBasePose(Emotion::kNeutral);

    Pose pose = progress <= 300
                    ? InterpolatePose(sleeping, sleepy, progress * 1000U / 300U)
                    : InterpolatePose(sleepy, neutral, (progress - 300U) * 1000U / 700U);

    const uint16_t blink = progress < 150 ? (1000U - progress * 4U) : 400U;
    if (pose.height > static_cast<int>(blink / 10U)) {
        pose.height = static_cast<int>(blink / 10U);
    }
    return pose;
}

uint16_t MhaiBotFaceV2::ClampProgress(uint32_t elapsed_ms, uint32_t duration_ms) {
    if (duration_ms == 0 || elapsed_ms >= duration_ms) {
        return 1000;
    }
    return static_cast<uint16_t>((elapsed_ms * 1000U) / duration_ms);
}

int MhaiBotFaceV2::LerpInt(int from, int to, uint16_t progress_per_mille) {
    return from + ((to - from) * static_cast<int>(progress_per_mille)) / 1000;
}

void MhaiBotFaceV2::TickTimerCb(lv_timer_t* timer) {
    auto* self = static_cast<MhaiBotFaceV2*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->destroying_ || !self->visible_) {
        return;
    }
    if (self->root_ == nullptr || !lv_obj_is_valid(self->root_)) {
        return;
    }
    const uint32_t now = lv_tick_get();
    const uint32_t elapsed = now - self->last_tick_ms_;
    self->last_tick_ms_ = now;
    self->Tick(elapsed);
}

void MhaiBotFaceV2::OnRootDeleted(lv_event_t* event) {
    auto* self = static_cast<MhaiBotFaceV2*>(lv_event_get_user_data(event));
    if (self == nullptr || self->destroying_) {
        return;
    }
    self->StopTimer();
    self->renderer_.Deinit();
    self->root_ = nullptr;
    self->left_eye_ = nullptr;
    self->right_eye_ = nullptr;
    self->sleep_label_ = nullptr;
    self->visible_ = false;
}
