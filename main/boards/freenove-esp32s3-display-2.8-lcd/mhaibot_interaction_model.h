#ifndef MHAIBOT_INTERACTION_MODEL_H
#define MHAIBOT_INTERACTION_MODEL_H

#include <cstdint>
#include <string_view>

enum class MhaiBotAlert { kNone, kBatteryLow, kError };

class MhaiBotPetGestureDetector {
public:
    bool Update(bool touched, uint16_t x, uint16_t y, uint32_t now_ms);
    void Reset();

private:
    bool active_ = false;
    uint16_t start_y_ = 0;
    int16_t leg_start_x_ = 0;
    int8_t direction_ = 0;
    uint8_t reversals_ = 0;
    uint32_t started_ms_ = 0;
};

uint16_t MhaiBotPetZoneTop();
uint16_t MhaiBotPetZoneBottom();
uint16_t MhaiBotPetMinStrokePx();
uint16_t MhaiBotPetMaxVerticalDriftPx();
uint32_t MhaiBotPetTimeoutMs();
uint32_t MhaiBotPetDurationMs();
uint32_t MhaiBotIdleSleepTimeoutSeconds();
uint32_t MhaiBotScreenOffIdleSeconds();
uint32_t MhaiBotGroggyWakeDurationMs();

std::string_view MhaiBotSleepText(uint32_t elapsed_ms);
uint16_t MhaiBotGroggyProgressPerMille(uint32_t elapsed_ms);
uint8_t MhaiBotGroggyBrightness(uint32_t elapsed_ms, uint8_t target_brightness);
MhaiBotAlert MhaiBotResolveAlert(bool error_active, bool battery_low);

#endif  // MHAIBOT_INTERACTION_MODEL_H
