#include "eye_intent_mailbox.h"

void EyeIntentMailbox::Publish(const EyeIntent& intent) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ = intent;
    has_value_ = true;
}

bool EyeIntentMailbox::ConsumeLatest(EyeIntent* out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_value_) {
        return false;
    }
    if (out != nullptr) {
        *out = latest_;
    }
    return true;
}
