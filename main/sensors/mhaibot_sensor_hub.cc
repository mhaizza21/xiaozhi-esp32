#include "mhaibot_sensor_hub.h"

#include <cmath>
#include <cstring>

#include <esp_log.h>

#define TAG "MhaibotSensorHub"

namespace {
constexpr uint8_t kQmi8658Address = 0x6B;
constexpr uint8_t kQmi8658WhoAmI = 0x00;
constexpr uint8_t kQmi8658Revision = 0x01;
constexpr uint8_t kQmi8658Ctrl2 = 0x03;
constexpr uint8_t kQmi8658Ctrl3 = 0x04;
constexpr uint8_t kQmi8658Ctrl7 = 0x08;
constexpr uint8_t kQmi8658AccXL = 0x35;
constexpr uint8_t kQmi8658TempL = 0x33;
constexpr float kQmi8658AccelSensitivity4g = 8192.0f;
constexpr float kQmi8658GyroSensitivity512dps = 64.0f;
constexpr float kQmi8658TempSensitivity = 256.0f;

constexpr uint8_t kPcf85063Address = 0x51;
constexpr uint8_t kPcf85063Seconds = 0x04;
constexpr uint8_t kPcf85063ClockIntegrityMask = 0x80;

int16_t Le16(const uint8_t* data) {
    return static_cast<int16_t>((static_cast<uint16_t>(data[1]) << 8) | data[0]);
}

uint8_t BcdToDec(uint8_t value) {
    return static_cast<uint8_t>(((value >> 4) * 10) + (value & 0x0f));
}

void AddEspError(cJSON* json, const char* key, esp_err_t status) {
    cJSON_AddStringToObject(json, key, esp_err_to_name(status));
}

void AddFloat(cJSON* json, const char* key, float value) {
    cJSON_AddNumberToObject(json, key, std::isfinite(value) ? value : 0.0f);
}
}  // namespace

MhaibotSensorHub::I2cRegisterDevice::I2cRegisterDevice(i2c_master_bus_handle_t bus, uint8_t address)
    : bus_(bus), address_(address) {
}

esp_err_t MhaibotSensorHub::I2cRegisterDevice::Init() {
    if (initialized_) {
        return ESP_OK;
    }

    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = address_;
    cfg.scl_speed_hz = 400 * 1000;

    esp_err_t ret = i2c_master_bus_add_device(bus_, &cfg, &device_);
    if (ret == ESP_OK) {
        initialized_ = true;
    }
    return ret;
}

esp_err_t MhaibotSensorHub::I2cRegisterDevice::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_transmit(device_, data, sizeof(data), 100);
}

esp_err_t MhaibotSensorHub::I2cRegisterDevice::ReadReg(uint8_t reg, uint8_t& value) {
    return i2c_master_transmit_receive(device_, &reg, sizeof(reg), &value, sizeof(value), 100);
}

esp_err_t MhaibotSensorHub::I2cRegisterDevice::ReadRegs(uint8_t reg, uint8_t* data, size_t length) {
    return i2c_master_transmit_receive(device_, &reg, sizeof(reg), data, length, 100);
}

MhaibotSensorHub::MhaibotSensorHub(i2c_master_bus_handle_t i2c_bus)
    : i2c_bus_(i2c_bus), motion_(i2c_bus, kQmi8658Address), rtc_(i2c_bus, kPcf85063Address) {
}

void MhaibotSensorHub::InitializeMotion() {
    motion_status_ = motion_.Init();
    if (motion_status_ != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658 not initialized: %s", esp_err_to_name(motion_status_));
        return;
    }

    motion_status_ = motion_.WriteReg(kQmi8658Ctrl2, 0x41);  // 4g, 250Hz
    if (motion_status_ == ESP_OK) {
        motion_status_ = motion_.WriteReg(kQmi8658Ctrl3, 0x55);  // 512dps, 250Hz
    }
    if (motion_status_ == ESP_OK) {
        motion_status_ = motion_.WriteReg(kQmi8658Ctrl7, 0x03);  // accelerometer + gyroscope
    }

    ESP_LOGI(TAG, "QMI8658 initialization status: %s", esp_err_to_name(motion_status_));
}

void MhaibotSensorHub::InitializeRtc() {
    rtc_status_ = rtc_.Init();
    ESP_LOGI(TAG, "PCF85063 initialization status: %s", esp_err_to_name(rtc_status_));
}

void MhaibotSensorHub::SetTouchInitialized(bool lvgl_registered) {
    touch_.initialized = true;
    touch_.lvgl_registered = lvgl_registered;
    touch_.last_event = lvgl_registered ? "lvgl_registered" : "driver_initialized";
}

void MhaibotSensorHub::RecordTouchEvent(const char* event_name) {
    touch_.initialized = true;
    touch_.last_event = event_name ? event_name : "unknown";
}

bool MhaibotSensorHub::ReadMotion(MhaibotMotionSample& sample) {
    if (motion_status_ != ESP_OK) {
        return false;
    }

    uint8_t identity[2] = {};
    esp_err_t ret = motion_.ReadRegs(kQmi8658WhoAmI, identity, sizeof(identity));
    if (ret != ESP_OK) {
        motion_status_ = ret;
        return false;
    }

    uint8_t temp[2] = {};
    ret = motion_.ReadRegs(kQmi8658TempL, temp, sizeof(temp));
    if (ret != ESP_OK) {
        motion_status_ = ret;
        return false;
    }

    uint8_t motion_data[12] = {};
    ret = motion_.ReadRegs(kQmi8658AccXL, motion_data, sizeof(motion_data));
    if (ret != ESP_OK) {
        motion_status_ = ret;
        return false;
    }

    sample.valid = true;
    sample.who_am_i = identity[0];
    sample.revision = identity[1];
    sample.temperature_c = static_cast<float>(Le16(temp)) / kQmi8658TempSensitivity;
    sample.accel_g[0] = static_cast<float>(Le16(&motion_data[0])) / kQmi8658AccelSensitivity4g;
    sample.accel_g[1] = static_cast<float>(Le16(&motion_data[2])) / kQmi8658AccelSensitivity4g;
    sample.accel_g[2] = static_cast<float>(Le16(&motion_data[4])) / kQmi8658AccelSensitivity4g;
    sample.gyro_dps[0] = static_cast<float>(Le16(&motion_data[6])) / kQmi8658GyroSensitivity512dps;
    sample.gyro_dps[1] = static_cast<float>(Le16(&motion_data[8])) / kQmi8658GyroSensitivity512dps;
    sample.gyro_dps[2] = static_cast<float>(Le16(&motion_data[10])) / kQmi8658GyroSensitivity512dps;
    return true;
}

bool MhaibotSensorHub::ReadRtcTime(MhaibotRtcTime& time) {
    if (rtc_status_ != ESP_OK) {
        return false;
    }

    uint8_t data[7] = {};
    esp_err_t ret = rtc_.ReadRegs(kPcf85063Seconds, data, sizeof(data));
    if (ret != ESP_OK) {
        rtc_status_ = ret;
        return false;
    }

    time.valid = true;
    time.clock_integrity_ok = (data[0] & kPcf85063ClockIntegrityMask) == 0;
    time.second = BcdToDec(data[0] & 0x7f);
    time.minute = BcdToDec(data[1] & 0x7f);
    time.hour = BcdToDec(data[2] & 0x3f);
    time.day = BcdToDec(data[3] & 0x3f);
    time.weekday = data[4] & 0x07;
    time.month = BcdToDec(data[5] & 0x1f);
    time.year = 2000 + BcdToDec(data[6]);
    return true;
}

cJSON* MhaibotSensorHub::GetStatusJson() {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "board_sensor_profile", "waveshare-esp32-s3-touch-lcd-1.69");
    cJSON_AddBoolToObject(json, "touch_initialized", touch_.initialized);
    cJSON_AddBoolToObject(json, "touch_lvgl_registered", touch_.lvgl_registered);
    AddEspError(json, "motion_status", motion_status_);
    AddEspError(json, "rtc_status", rtc_status_);
    cJSON_AddBoolToObject(json, "pir_ready", false);
    cJSON_AddBoolToObject(json, "camera_ready", false);
    cJSON_AddStringToObject(json, "pir_note", "Architecture placeholder only; GPIO is not assigned yet.");
    cJSON_AddStringToObject(json, "camera_note", "Architecture placeholder only; camera module and pin map are not selected yet.");
    return json;
}

cJSON* MhaibotSensorHub::GetMotionJson() {
    MhaibotMotionSample sample;
    bool ok = ReadMotion(sample);
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "valid", ok);
    AddEspError(json, "status", motion_status_);
    if (!ok) {
        return json;
    }

    cJSON_AddNumberToObject(json, "who_am_i", sample.who_am_i);
    cJSON_AddNumberToObject(json, "revision", sample.revision);
    AddFloat(json, "temperature_c", sample.temperature_c);

    cJSON* accel = cJSON_CreateObject();
    AddFloat(accel, "x_g", sample.accel_g[0]);
    AddFloat(accel, "y_g", sample.accel_g[1]);
    AddFloat(accel, "z_g", sample.accel_g[2]);
    cJSON_AddItemToObject(json, "accelerometer", accel);

    cJSON* gyro = cJSON_CreateObject();
    AddFloat(gyro, "x_dps", sample.gyro_dps[0]);
    AddFloat(gyro, "y_dps", sample.gyro_dps[1]);
    AddFloat(gyro, "z_dps", sample.gyro_dps[2]);
    cJSON_AddItemToObject(json, "gyroscope", gyro);
    return json;
}

cJSON* MhaibotSensorHub::GetRtcJson() {
    MhaibotRtcTime time;
    bool ok = ReadRtcTime(time);
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "valid", ok);
    AddEspError(json, "status", rtc_status_);
    if (!ok) {
        return json;
    }

    cJSON_AddBoolToObject(json, "clock_integrity_ok", time.clock_integrity_ok);
    cJSON_AddNumberToObject(json, "year", time.year);
    cJSON_AddNumberToObject(json, "month", time.month);
    cJSON_AddNumberToObject(json, "day", time.day);
    cJSON_AddNumberToObject(json, "weekday", time.weekday);
    cJSON_AddNumberToObject(json, "hour", time.hour);
    cJSON_AddNumberToObject(json, "minute", time.minute);
    cJSON_AddNumberToObject(json, "second", time.second);
    return json;
}

cJSON* MhaibotSensorHub::GetTouchJson() {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "initialized", touch_.initialized);
    cJSON_AddBoolToObject(json, "lvgl_registered", touch_.lvgl_registered);
    cJSON_AddStringToObject(json, "last_event", touch_.last_event.c_str());
    cJSON_AddStringToObject(json, "note", "Detailed touch gestures are handled by LVGL; this diagnostic reports board-level bring-up state.");
    return json;
}
