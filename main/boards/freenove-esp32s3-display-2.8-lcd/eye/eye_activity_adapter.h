#ifndef MHAIBOT_EYE_ACTIVITY_ADAPTER_H
#define MHAIBOT_EYE_ACTIVITY_ADAPTER_H

#include "eye_intent.h"

#include "device_state.h"

// Board-local DeviceState / SetEmotion(...) -> EyeIntent mapping (02 / ADR-002).
// Pure mapping only: never draws, never owns DeviceState transition legality,
// and does not duplicate the DeviceState state machine (ADR-001).
class EyeActivityAdapter {
public:
    // state: Application::GetInstance().GetDeviceState() at call time.
    // emotion: the same C string passed to Display::SetEmotion (may be
    //   nullptr); used both for the EyeEmotion mapping and for the ordered
    //   activity overrides (sleeping/thinking/listening cues) per ADR-002.
    // groggy_wake_active: board groggy-wake transient state (e.g.
    //   MhaiBotFaceV2::IsGroggyWakeActive()) — the sleeping override reads
    //   the emotion string directly instead of a separate flag, since
    //   SetEmotion("sleeping"/"sleep") is exactly when the board's sleeping
    //   face is active.
    EyeIntent FromDeviceState(DeviceState state, const char* emotion,
                               bool groggy_wake_active) const;

private:
    static EyeActivity BaseActivity(DeviceState state);
    static EyeEmotion MapEmotion(const char* emotion);
};

#endif  // MHAIBOT_EYE_ACTIVITY_ADAPTER_H
