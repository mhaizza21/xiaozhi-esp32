#ifndef MHAIBOT_TEST_EYE_PARITY_HARNESS_H
#define MHAIBOT_TEST_EYE_PARITY_HARNESS_H

#include "eye_frame.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// ============================================================================
// Slice 9 — Option A shadow parity comparison harness (host-only).
// ============================================================================
//
// Comparison point (09 "Slice 9 — Shadow dual-path compare", "Option A —
// canonical pre-compose"):
//
//   Legacy: MhaiBotFaceV2::ResolveRenderedPose() -> PoseToEyeFrame()
//   Shadow: EmotionController -> EyeAnimationCoordinator::Compose()
//
//   ...compared BEFORE ApplyIdleGaze() and BEFORE ApplyBlinkOpenness() on
//   both sides. Blink openness and idle gaze/glance offsets are additive
//   post-compose behavior that both paths share identically (07 §9,
//   ADR-004) — folding them in before comparison would create false
//   mismatches unrelated to the canonical geometry this harness verifies.
//
// This harness never drives pixels and never calls into
// MhaiBotFaceV2::ApplyPose. Legacy remains the sole pixel-authoritative
// path; nothing under main/boards/.../mhaibot_face_v2.cc is modified by
// Slice 9.
//
// ----------------------------------------------------------------------
// Comparison contract (derived from the Slice 8 final parity audit)
// ----------------------------------------------------------------------
//
// INCLUDED — compared with PASS WITH TOLERANCE:
//   1. Steady-state emotion transition           tolerance: <=1px
//   2. GroggyWake, full 0-5000ms                  tolerance: <=1px,
//                                                  exact match required in
//                                                  the pseudo-blink clamp
//                                                  window
//   3. Startle, ONLY while the emotion resolves to one of:
//        Neutral, Robot2, Happy, Confident/Focused, Relaxed/Sleepy
//                                                  tolerance: <=2px
//                                                  (see "Startle tolerance"
//                                                  note below — this is
//                                                  wider than the other
//                                                  three for a specific,
//                                                  proven mathematical
//                                                  reason, not a looser
//                                                  standard)
//   4. Pet, ONLY at t >= 300ms (post-ramp hold)   tolerance: <=1px
//
// Startle tolerance — why <=2px instead of <=1px:
//   StartledPose()/StartledBase() change BOTH the per-eye origin-x position
//   (left_x-7 / right_x+7) AND the shared width (+14) in the same
//   interpolation, and center_x = origin_x + width/2. Legacy truncates
//   left_x, right_x, and width independently to an int at every tick
//   (LerpInt); the shadow path computes all three as exact floats
//   (LerpFloat). The two independent truncation errors compound through
//   the center conversion: |center_x error| <= |position truncation
//   error| + 0.5 * |width truncation error| < 1px + 0.5*1px = 1.5px in the
//   worst case. This is not a bug in either path and not fixable by
//   reordering the shadow math (lerping origin+width separately and
//   lerping center directly are provably identical for linear
//   interpolation) — it is an unavoidable consequence of legacy's integer
//   truncation under a simultaneous position+width change. Verified
//   empirically: full 0-700ms sweep across all 6 representable Startle
//   emotions peaks at 1.498px (t=600ms, Neutral), matching the 1.5px
//   analytical bound; Steady transition (position/height only, no width
//   change) peaks at 0.968px; Groggy (full 0-5000ms) peaks at 0.998px —
//   both safely under 1px, confirming the compounding is specific to
//   Startle's simultaneous width change. <=2px is the bound rounded up
//   from the proven 1.5px worst case, not an arbitrarily loosened number.
//
// EXCLUDED — never asserted for parity in Slice 9 (do not "fix" in
// Slice 9; these are architecture-level gaps between the legacy and
// canonical models, not defects):
//   1. Pet geometry at t < 300ms (ramp-in).
//      Reason: legacy's ResolveRenderedPose() re-interpolates from its own
//      previous rendered output every tick (a recursive re-lerp), while
//      the shadow path lerps once from a captured anchor. The curves
//      diverge in shape (peak delta ~16px observed at t=165ms for a
//      Neutral->Pet transition); this is not a rounding artifact and has
//      no tolerance that makes it "pass" meaningfully.
//   2. Pet sway/bob/opacity shimmer, at any t.
//      Reason: legacy phases these off `frame_`, a Tick()-call counter
//      that never resets and already holds an arbitrary value when a pet
//      session starts. The shadow path phases off `transient_elapsed_ms_`
//      (always 0 at pet start). No time-based transform recovers legacy's
//      phase from shadow state.
//   3. Same-emotion repeated SetEmotion/SetIntent while a transient or
//      transition is already active.
//      Reason: legacy's SetEmotion is not edge-triggered on emotion value
//      (it always cancels the active transient and restarts the 300ms
//      transition); Coordinator::SetIntent is edge-triggered strictly on
//      `intent.emotion != intent_.emotion`. EyeIntent carries no event
//      identity/sequence number to distinguish "same value, new event"
//      from "no new event" (05/02 as written).
//   4. Startle while the emotion resolves to Thinking, Listening, or
//      Speaking.
//      Reason: legacy's StartledPose() sources from
//      ResolveBasePose(target_emotion_), which has distinct baked-in
//      geometry for these three legacy Emotion values (kThinking/
//      kListening/kSpeaking). EyeEmotion has no equivalent members for
//      them (ADR-002 routes them through EyeActivity instead), and
//      Coordinator::ComposeStartled() deliberately bypasses the activity
//      delta, so the shadow path cannot reproduce that geometry.
//
// Slice 9 does not attempt to close any of the four exclusions above.

struct ParityFieldDelta {
    const char* field = "";
    float legacy = 0.0f;
    float shadow = 0.0f;
    float abs_delta = 0.0f;
};

struct ParityResult {
    bool within_tolerance = true;
    ParityFieldDelta worst;
};

namespace detail {

inline void Track(ParityResult& result, const char* name, float legacy, float shadow,
                   float tolerance_px) {
    const float delta = std::fabs(shadow - legacy);
    if (delta > result.worst.abs_delta) {
        result.worst = ParityFieldDelta{name, legacy, shadow, delta};
    }
    if (delta > tolerance_px) {
        result.within_tolerance = false;
    }
}

}  // namespace detail

// Compares two canonical EyeFrames field-by-field against `tolerance_px`.
// `compare_opacity` should be false for Pet-at-t>=300ms comparisons, per
// exclusion #2 above (opacity shimmer is excluded even past the ramp-in
// window) — callers building a Pet frame for comparison should pass a
// fixed neutral opacity (e.g. 1.0f) on both sides and set
// compare_opacity=false so this function does not silently assert on it.
inline ParityResult CompareCanonicalFrames(const EyeFrame& legacy, const EyeFrame& shadow,
                                            float tolerance_px, bool compare_opacity = true) {
    ParityResult result;
    detail::Track(result, "left.center_x", legacy.left.center_x, shadow.left.center_x, tolerance_px);
    detail::Track(result, "left.center_y", legacy.left.center_y, shadow.left.center_y, tolerance_px);
    detail::Track(result, "left.width", legacy.left.width, shadow.left.width, tolerance_px);
    detail::Track(result, "left.height", legacy.left.height, shadow.left.height, tolerance_px);
    detail::Track(result, "left.corner_radius", legacy.left.corner_radius, shadow.left.corner_radius,
                  tolerance_px);
    detail::Track(result, "left.rotation_degrees", legacy.left.rotation_degrees,
                  shadow.left.rotation_degrees, tolerance_px);
    detail::Track(result, "right.center_x", legacy.right.center_x, shadow.right.center_x, tolerance_px);
    detail::Track(result, "right.center_y", legacy.right.center_y, shadow.right.center_y, tolerance_px);
    detail::Track(result, "right.width", legacy.right.width, shadow.right.width, tolerance_px);
    detail::Track(result, "right.height", legacy.right.height, shadow.right.height, tolerance_px);
    detail::Track(result, "right.corner_radius", legacy.right.corner_radius, shadow.right.corner_radius,
                  tolerance_px);
    detail::Track(result, "right.rotation_degrees", legacy.right.rotation_degrees,
                  shadow.right.rotation_degrees, tolerance_px);
    if (compare_opacity) {
        // Opacity is unitless [0,1], not pixels — reuse tolerance_px as a
        // normalized-unit tolerance only where opacity is actually
        // asserted (Steady/Groggy/Startle all hold fixed opacity=1.0 on
        // both sides, so any tolerance > 0 passes trivially there).
        detail::Track(result, "opacity", legacy.opacity, shadow.opacity, tolerance_px);
    }
    return result;
}

#endif  // MHAIBOT_TEST_EYE_PARITY_HARNESS_H
