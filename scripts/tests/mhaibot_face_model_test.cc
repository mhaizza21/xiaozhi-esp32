#include "mhaibot_interaction_model.h"

#include <cassert>

int main() {
    assert(MhaiBotPetZoneTop() == 32);
    assert(MhaiBotPetZoneBottom() == 104);
    assert(MhaiBotPetMinStrokePx() == 40);
    assert(MhaiBotPetMaxVerticalDriftPx() == 24);
    assert(MhaiBotPetTimeoutMs() == 2000);
    assert(MhaiBotPetDurationMs() == 3000);
    assert(MhaiBotIdleSleepTimeoutSeconds() == 600);
    assert(MhaiBotScreenOffIdleSeconds() == 2400);
    assert(MhaiBotGroggyWakeDurationMs() == 5000);

    assert(MhaiBotSleepText(0) == "Z");
    assert(MhaiBotSleepText(500) == "Zz");
    assert(MhaiBotSleepText(1000) == "Zzz");
    assert(MhaiBotSleepText(1500).empty());
    assert(MhaiBotSleepText(2000) == "Z");

    assert(MhaiBotGroggyProgressPerMille(0) == 0);
    assert(MhaiBotGroggyProgressPerMille(2500) == 500);
    assert(MhaiBotGroggyProgressPerMille(5000) == 1000);
    assert(MhaiBotGroggyProgressPerMille(6000) == 1000);

    assert(MhaiBotGroggyBrightness(0, 75) == 20);
    assert(MhaiBotGroggyBrightness(2500, 75) == 47);
    assert(MhaiBotGroggyBrightness(5000, 75) == 75);
    assert(MhaiBotGroggyBrightness(6000, 75) == 75);
    assert(MhaiBotGroggyBrightness(2500, 12) == 20);

    assert(MhaiBotResolveAlert(false, false) == MhaiBotAlert::kNone);
    assert(MhaiBotResolveAlert(false, true) == MhaiBotAlert::kBatteryLow);
    assert(MhaiBotResolveAlert(true, false) == MhaiBotAlert::kError);
    assert(MhaiBotResolveAlert(true, true) == MhaiBotAlert::kError);

    assert(MhaiBotLowBatteryOnThresholdPercent() == 15);
    assert(MhaiBotLowBatteryOffThresholdPercent() == 20);
    // 16% initial state -> OFF (above the on-threshold, nothing latched yet)
    assert(!MhaiBotBatteryLowWithHysteresis(false, false, true, 16));
    // 15% discharging -> ON (at the on-threshold)
    assert(MhaiBotBatteryLowWithHysteresis(false, false, true, 15));
    // 14% discharging -> ON (below the on-threshold)
    assert(MhaiBotBatteryLowWithHysteresis(false, false, true, 14));
    // 19% after low state -> remains ON (below the off-threshold)
    assert(MhaiBotBatteryLowWithHysteresis(true, false, true, 19));
    // 20% after low state -> OFF (at the off-threshold)
    assert(!MhaiBotBatteryLowWithHysteresis(true, false, true, 20));
    // 10% while charging -> OFF regardless of level
    assert(!MhaiBotBatteryLowWithHysteresis(false, true, true, 10));
    assert(!MhaiBotBatteryLowWithHysteresis(true, true, true, 10));
    // Not discharging (e.g. no battery / on external power) -> OFF
    assert(!MhaiBotBatteryLowWithHysteresis(true, false, false, 10));

    MhaiBotPetGestureDetector valid;
    assert(!valid.Update(true, 20, 60, 0));
    assert(!valid.Update(true, 70, 62, 300));
    assert(!valid.Update(true, 20, 61, 600));
    assert(!valid.Update(true, 70, 63, 900));
    assert(valid.Update(true, 20, 62, 1200));
    assert(!valid.Update(false, 20, 62, 1300));

    MhaiBotPetGestureDetector tap;
    assert(!tap.Update(true, 40, 60, 0));
    assert(!tap.Update(false, 40, 60, 100));

    MhaiBotPetGestureDetector one_way;
    assert(!one_way.Update(true, 20, 60, 0));
    assert(!one_way.Update(true, 200, 60, 500));
    assert(!one_way.Update(false, 200, 60, 600));

    MhaiBotPetGestureDetector drift;
    assert(!drift.Update(true, 20, 40, 0));
    assert(!drift.Update(true, 80, 70, 300));

    MhaiBotPetGestureDetector timeout;
    assert(!timeout.Update(true, 20, 60, 0));
    assert(!timeout.Update(true, 70, 60, 700));
    assert(!timeout.Update(true, 20, 60, 1400));
    assert(!timeout.Update(true, 70, 60, 2100));
    assert(!timeout.Update(true, 20, 60, 2400));

    MhaiBotPetGestureDetector outside_zone;
    assert(!outside_zone.Update(true, 20, 31, 0));
    assert(!outside_zone.Update(true, 70, 60, 100));

    MhaiBotPetGestureDetector leaves_zone;
    assert(!leaves_zone.Update(true, 20, 100, 0));
    assert(!leaves_zone.Update(true, 80, 110, 300));
    assert(!leaves_zone.Update(true, 20, 100, 600));

    return 0;
}
