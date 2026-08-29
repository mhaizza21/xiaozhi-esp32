#ifndef MHAIBOT_SENSOR_HUB_H
#define MHAIBOT_SENSOR_HUB_H

#include <cJSON.h>
#include <driver/i2c_master.h>
#include <esp_err.h>

#include <cstdint>
#include <string>

struct MhaibotMotionSample {
    bool valid = false;
    float accel_g[3] = {0.0f, 0.0f, 0.0f};
    float gyro_dps[3] = {0.0f, 0.0f, 0.0f};
    float temperature_c = 0.0f;
    uint8_t who_am_i = 0;
    uint8_t revision = 0;
};

struct MhaibotRtcTime {
    bool valid = false;
    bool clock_integrity_ok = false;
    int year = 0;
    int month = 0;
    int day = 0;
    int weekday = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

struct MhaibotTouchStatus {
    bool initialized = false;
    bool lvgl_registered = false;
    std::string last_event = "not_observed";
    uint32_t event_count = 0;
    int64_t last_event_us = 0;
};

struct MhaibotMotionContext {
    bool valid = false;
    float accel_magnitude_g = 0.0f;
    float gyro_magnitude_dps = 0.0f;
    float roll_deg = 0.0f;
    float pitch_deg = 0.0f;
    bool stable = false;
    bool tilted = false;
    bool moving = false;
    bool shake_like_motion = false;
    bool impact_like_motion = false;
    bool low_gravity = false;
    std::string posture = "unknown";
    std::string primary_event = "unavailable";
    std::string suggested_face = "neutral";
};

class MhaibotSensorHub {
public:
    explicit MhaibotSensorHub(i2c_master_bus_handle_t i2c_bus);

    void InitializeMotion();
    void InitializeRtc();
    void SetTouchInitialized(bool lvgl_registered);
    void RecordTouchEvent(const char* event_name);

    bool ReadMotion(MhaibotMotionSample& sample);
    bool ReadRtcTime(MhaibotRtcTime& time);

    cJSON* GetStatusJson();
    cJSON* GetMotionJson();
    cJSON* GetRtcJson();
    cJSON* GetTouchJson();
    cJSON* GetInteractionContextJson();

private:
    class I2cRegisterDevice {
    public:
        I2cRegisterDevice(i2c_master_bus_handle_t bus, uint8_t address);

        esp_err_t Init();
        esp_err_t WriteReg(uint8_t reg, uint8_t value);
        esp_err_t ReadReg(uint8_t reg, uint8_t& value);
        esp_err_t ReadRegs(uint8_t reg, uint8_t* data, size_t length);
        bool initialized() const { return initialized_; }

    private:
        i2c_master_bus_handle_t bus_;
        i2c_master_dev_handle_t device_ = nullptr;
        uint8_t address_;
        bool initialized_ = false;
    };

    i2c_master_bus_handle_t i2c_bus_;
    I2cRegisterDevice motion_;
    I2cRegisterDevice rtc_;
    MhaibotTouchStatus touch_;
    esp_err_t motion_status_ = ESP_ERR_NOT_FOUND;
    esp_err_t rtc_status_ = ESP_ERR_NOT_FOUND;

    MhaibotMotionContext InterpretMotion(const MhaibotMotionSample& sample) const;
};

#endif  // MHAIBOT_SENSOR_HUB_H
