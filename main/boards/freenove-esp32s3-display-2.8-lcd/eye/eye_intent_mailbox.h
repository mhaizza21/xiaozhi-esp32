#ifndef MHAIBOT_EYE_INTENT_MAILBOX_H
#define MHAIBOT_EYE_INTENT_MAILBOX_H

#include "eye_intent.h"

#include <mutex>

// Mutex-protected latest-wins intent slot (ADR-005). Any task may Publish a
// complete snapshot; only the LVGL-safe display/UI owner may ConsumeLatest.
// No heap allocation. Publish must not call LVGL or take DisplayLockGuard —
// this mailbox is not a substitute for the LVGL lock.
class EyeIntentMailbox {
public:
    // Any task: publish a complete snapshot. Overwrites the previous value.
    void Publish(const EyeIntent& intent);

    // Display/UI owner only: copy the latest snapshot.
    // Returns true if a value has ever been published (including unchanged).
    bool ConsumeLatest(EyeIntent* out) const;

private:
    mutable std::mutex mutex_;
    EyeIntent latest_{};
    bool has_value_ = false;
};

#endif  // MHAIBOT_EYE_INTENT_MAILBOX_H
