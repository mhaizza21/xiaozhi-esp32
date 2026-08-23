#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include <wifi_station.h>
#include "adc_battery_monitor.h"
#include "application.h"
#include "button.h"
#include "codecs/es8311_audio_codec.h"
#include "config.h"
#include "eye_pixel_source_console.h"
#include "mcp_server.h"
#include "mhaibot_display.h"
#include "mhaibot_interaction_model.h"
#include "wifi_board.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_lcd_ili9341.h"
#include "led/single_led.h"
#include "power_save_timer.h"
#include "system_reset.h"

#include <atomic>
#include <string>

#define TAG "FreenoveESP32S3Display"

namespace {
constexpr gpio_num_t kMhaiBotEmotionButtonGpio = GPIO_NUM_2;
constexpr uint32_t kMhaiBotEmotionButtonDebounceMs = 50;
}  // namespace

class MhaiBotBacklight : public PwmBacklight {
public:
    MhaiBotBacklight(gpio_num_t pin, bool output_invert) : PwmBacklight(pin, output_invert) {}

    void SetBrightnessImmediate(uint8_t brightness) {
        if (brightness > 100) {
            brightness = 100;
        }
        if (transition_timer_ != nullptr) {
            esp_timer_stop(transition_timer_);
        }
        brightness_ = brightness;
        target_brightness_ = brightness;
        SetBrightnessImpl(brightness);
    }
};

class TouchDriver {
public:
    TouchDriver() : dev_(nullptr) {}

    bool Init(i2c_master_bus_handle_t bus, uint8_t addr) {
        i2c_device_config_t cfg = {
            .device_address = addr,
            .scl_speed_hz = 400000,
            .scl_wait_us = 0,
        };
        return i2c_master_bus_add_device(bus, &cfg, &dev_) == ESP_OK;
    }

    bool Read(bool& touched, uint16_t& x, uint16_t& y) {
        touched = false;
        x = y = 0;
        if (!dev_)
            return false;

        uint8_t reg = 0x02;
        uint8_t buf[5];
        if (i2c_master_transmit_receive(dev_, &reg, 1, buf, 5, 50) != ESP_OK)
            return false;

        uint8_t points = buf[0] & 0x0F;
        if (points == 0)
            return true;

        touched = true;
        x = ((buf[1] & 0x0F) << 8) | buf[2];
        y = ((buf[3] & 0x0F) << 8) | buf[4];
        return true;
    }

private:
    i2c_master_dev_handle_t dev_;
};

class FreenoveESP32S3Display : public WifiBoard {
private:
    Button boot_button_;
    MhaiBotDisplay* display_;
    i2c_master_bus_handle_t codec_i2c_bus_;
    TouchDriver touch_;
    MhaiBotPetGestureDetector pet_gesture_;
    MhaiBotStartleTapDetector startle_tap_;
    AdcBatteryMonitor* adc_battery_monitor_;
    PowerSaveTimer* power_save_timer_ = nullptr;
    std::atomic<bool> screen_off_{false};
    std::atomic<bool> shutdown_applied_{false};
    std::atomic<bool> touch_wake_pending_{false};
    std::atomic<bool> suppress_touch_release_{false};
    std::atomic<bool> groggy_wake_active_{false};
    std::atomic<uint32_t> groggy_wake_started_ms_{0};
    std::atomic<uint32_t> next_groggy_brightness_update_ms_{0};
    std::atomic<uint32_t> sleep_generation_{0};
    std::atomic<uint8_t> groggy_target_brightness_{75};
    std::atomic<uint8_t> pre_sleep_brightness_{100};
    std::atomic<bool> sleeping_face_active_{false};
    uint8_t emotion_button_index_ = 0;

    void InitializeBatteryMonitor() {
        adc_battery_monitor_ =
            new AdcBatteryMonitor(ADC_UNIT_1, ADC_CHANNEL_8, 200000, 200000, GPIO_NUM_NC);
    }

    static void TouchTask(void* arg) {
        auto* self = static_cast<FreenoveESP32S3Display*>(arg);
        auto& app = Application::GetInstance();

        uint32_t last_tap = 0;
        uint32_t down_start = 0;
        bool down = false;
        bool emotion_button_last_raw = false;
        bool emotion_button_stable = false;
        uint32_t emotion_button_last_change = 0;

        while (true) {
            bool t;
            uint16_t x, y;
            self->touch_.Read(t, x, y);

            uint32_t now = esp_timer_get_time() / 1000;

            const bool emotion_button_raw =
                gpio_get_level(kMhaiBotEmotionButtonGpio) == 0;
            if (emotion_button_raw != emotion_button_last_raw) {
                emotion_button_last_raw = emotion_button_raw;
                emotion_button_last_change = now;
            }
            if (now - emotion_button_last_change >= kMhaiBotEmotionButtonDebounceMs &&
                emotion_button_raw != emotion_button_stable) {
                emotion_button_stable = emotion_button_raw;
                if (emotion_button_stable && !self->screen_off_.load() &&
                    !self->sleeping_face_active_.load() &&
                    !self->groggy_wake_active_.load()) {
                    app.Schedule([self]() { self->CycleEmotionButton(); });
                }
            }

            if (self->groggy_wake_active_.load() &&
                now >= self->next_groggy_brightness_update_ms_.load()) {
                self->next_groggy_brightness_update_ms_.store(now + 100);
                app.Schedule([self, now]() { self->UpdateGroggyWake(now); });
            }

            if (!self->screen_off_.load() && !self->sleeping_face_active_.load() &&
                !self->groggy_wake_active_.load()) {
                if (self->pet_gesture_.Update(t, x, y, now)) {
                    app.Schedule([self]() { self->display_->StartPetting(); });
                    self->suppress_touch_release_.store(true);
                }
                if (self->startle_tap_.Update(t, x, y, now)) {
                    app.Schedule([self]() { self->display_->StartStartled(); });
                    self->suppress_touch_release_.store(true);
                }
            }

            if (t) {
                if (!down) {
                    down = true;
                    down_start = now;
                    if (self->power_save_timer_ != nullptr) {
                        if (self->screen_off_.load()) {
                            self->touch_wake_pending_.store(true);
                            self->suppress_touch_release_.store(true);
                            self->power_save_timer_->WakeUp();
                        } else if (self->groggy_wake_active_.load()) {
                            self->suppress_touch_release_.store(true);
                        } else {
                            self->power_save_timer_->WakeUp();
                        }
                    }
                }
            }

            if (!t && down) {
                down = false;
                if (self->suppress_touch_release_.exchange(false)) {
                    last_tap = 0;
                    continue;
                }

                uint32_t press = now - down_start;

                // long tap
                if (press > 3000) {
                    app.Schedule([self]() { self->EnterWifiConfigMode(); });
                } else {
                    // double tap
                    if (now - last_tap < 250) {
                        app.Schedule([]() { Application::GetInstance().StartListening(); });
                        last_tap = 0;
                    } else {
                        // single tap
                        app.Schedule([]() { Application::GetInstance().ToggleChatState(); });
                        last_tap = now;
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    void CycleEmotionButton() {
        static constexpr const char* kEmotionCycle[] = {
            "happy",
            "thinking",
            "confident",
            "sleepy",
            "neutral",
        };
        const char* emotion = kEmotionCycle[emotion_button_index_];
        emotion_button_index_ =
            static_cast<uint8_t>((emotion_button_index_ + 1) %
                                 (sizeof(kEmotionCycle) / sizeof(kEmotionCycle[0])));
        ESP_LOGI(TAG, "Emotion button IO%d -> %s", static_cast<int>(kMhaiBotEmotionButtonGpio),
                 emotion);
        display_->CancelTransientAnimation();
        display_->SetEmotion(emotion);
    }

    void InitializeEmotionButton() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = 1ULL << kMhaiBotEmotionButtonGpio;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    void ApplyScreenOff() {
        if (shutdown_applied_.exchange(true)) {
            return;
        }
        screen_off_.store(true);
        sleeping_face_active_.store(false);
        groggy_wake_active_.store(false);
        display_->CancelTransientAnimation();
        GetBacklight()->SetBrightness(0);
        display_->SetPanelPowered(false);
    }

    void StartGroggyWake(uint32_t now_ms) {
        shutdown_applied_.store(false);
        screen_off_.store(false);
        sleeping_face_active_.store(false);
        groggy_wake_active_.store(true);
        groggy_wake_started_ms_.store(now_ms);
        next_groggy_brightness_update_ms_.store(now_ms + 100);
        groggy_target_brightness_.store(pre_sleep_brightness_.load());
        display_->SetPanelPowered(true);
        display_->StartGroggyWake();
        GetMhaiBotBacklight()->SetBrightnessImmediate(
            MhaiBotGroggyBrightness(0, groggy_target_brightness_.load()));
    }

    bool CancelGroggyWake() {
        const bool was_groggy = groggy_wake_active_.exchange(false);
        if (was_groggy) {
            display_->CancelTransientAnimation();
            GetBacklight()->RestoreBrightness();
        }
        return was_groggy;
    }

    void UpdateGroggyWake(uint32_t now_ms) {
        if (!groggy_wake_active_.load()) {
            return;
        }
        const uint32_t elapsed_ms = now_ms - groggy_wake_started_ms_.load();
        if (elapsed_ms >= MhaiBotGroggyWakeDurationMs()) {
            groggy_wake_active_.store(false);
            GetBacklight()->RestoreBrightness();
            display_->SetEmotion("neutral");
            return;
        }
        GetMhaiBotBacklight()->SetBrightnessImmediate(
            MhaiBotGroggyBrightness(elapsed_ms, groggy_target_brightness_.load()));
    }

    void WakeFromNonTouchInput() {
        sleep_generation_.fetch_add(1);
        touch_wake_pending_.store(false);
        shutdown_applied_.store(false);
        const bool was_screen_off = screen_off_.exchange(false);
        const bool was_sleeping = sleeping_face_active_.exchange(false);
        if (was_screen_off) {
            display_->SetPanelPowered(true);
        }
        if (!CancelGroggyWake() && (was_screen_off || was_sleeping)) {
            GetBacklight()->RestoreBrightness();
            display_->SetEmotion("neutral");
        }
        if (power_save_timer_ != nullptr) {
            power_save_timer_->WakeUp();
        }
    }

    void InitializeTouch() {
        if (!touch_.Init(codec_i2c_bus_, 0x38))
            return;
        xTaskCreatePinnedToCore(TouchTask, "touch_task", 4096, this, 5, nullptr, 0);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = AUDIO_CODEC_I2C_NUM,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags =
                {
                    .enable_internal_pullup = 1,
                },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = DISPLAY_MIS0_PIN;
        buscfg.sclk_io_num = DISPLAY_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            Application::GetInstance().Schedule([this]() {
                WakeFromNonTouchInput();
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    EnterWifiConfigMode();
                }
                app.ToggleChatState();
            });
        });
    }

    void InitializePowerSaveTimer() {
        // Keep CPU/audio behavior unchanged. The board-local display first
        // shows sleeping eyes, then powers the panel off after 30 more minutes.
        // Mic/WakeNet must stay live through sleep so voice wake ("Hi ESP")
        // keeps working -- see main/application.cc's wake-word cooldown for
        // how the self-trigger-after-TTS loop is handled instead.
        power_save_timer_ =
            new PowerSaveTimer(-1, MhaiBotIdleSleepTimeoutSeconds(), MhaiBotScreenOffIdleSeconds());
        power_save_timer_->OnEnterSleepMode([this]() {
            Application::GetInstance().Schedule([this]() {
                ESP_LOGI(TAG, "Idle timeout reached, entering MhaiBot sleep face");
                sleep_generation_.fetch_add(1);
                pre_sleep_brightness_.store(GetBacklight()->brightness());
                sleeping_face_active_.store(true);
                display_->CancelTransientAnimation();
                display_->SetEmotion("sleeping");
                GetBacklight()->SetBrightness(8);
            });
        });
        power_save_timer_->OnExitSleepMode([this]() {
            const bool touch_wake = touch_wake_pending_.exchange(false);
            const uint32_t now_ms = esp_timer_get_time() / 1000;
            Application::GetInstance().Schedule([this, touch_wake, now_ms]() {
                if (touch_wake) {
                    StartGroggyWake(now_ms);
                    return;
                }
                WakeFromNonTouchInput();
            });
        });
        power_save_timer_->OnShutdownRequest([this]() {
            const uint32_t requested_generation = sleep_generation_.load();
            Application::GetInstance().Schedule([this, requested_generation]() {
                if (requested_generation == sleep_generation_.load() &&
                    sleeping_face_active_.load()) {
                    ApplyScreenOff();
                }
            });
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_SPI_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        ESP_LOGI(TAG, "Install LCD driver ILI9341");
        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new MhaiBotDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                      DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
                                      DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    // Slice 11B: registers the engineer-only `eye_pixel_source` UART
    // console command (bring-up/soak tool, not user-facing). display_ is
    // already constructed by InitializeLcdDisplay() at this point in the
    // constructor's call order.
    void InitializeTools() {
        RegisterEyePixelSourceConsole(display_);

        McpServer::GetInstance().AddTool(
            "self.neck.move",
            "Move MhaiBot's neck with safe bounded servo gestures. Use this when the user asks the robot to look left, look right, look up, look down, center, shake its head, or nod. Allowed action values: left, right, up, down, center, shake, nod.",
            PropertyList({
                Property("action", kPropertyTypeString),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                const std::string action = properties["action"].value<std::string>();
                if (!display_->MoveNeck(action)) {
                    return std::string(
                        "Unsupported neck action. Use left, right, up, down, center, shake, or nod.");
                }
                return std::string("Neck action sent: " + action);
            });
    }

public:
    FreenoveESP32S3Display() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeBatteryMonitor();
        InitializeSpi();
        InitializeLcdDisplay();
        InitializePowerSaveTimer();
        InitializeEmotionButton();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->SetBrightness(100);
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_, AUDIO_CODEC_I2C_NUM, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR, true, true);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override { return display_; }

    virtual Backlight* GetBacklight() override {
        static MhaiBotBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    MhaiBotBacklight* GetMhaiBotBacklight() {
        return static_cast<MhaiBotBacklight*>(GetBacklight());
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            Application::GetInstance().Schedule([this]() { WakeFromNonTouchInput(); });
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        charging = adc_battery_monitor_->IsCharging();
        discharging = adc_battery_monitor_->IsDischarging();
        level = adc_battery_monitor_->GetBatteryLevel();
        return true;
    }

    // Lowered from the model default (~0.63) to the minimum allowed
    // (0.4-0.9999). Hardware-validated: "Hi ESP" reliably wakes the device
    // from real sleep at this threshold, at normal speaking volume and
    // distance, on this board's mic/codec path. Future tuning may revisit
    // this if false wakes from ambient noise become an issue.
    virtual std::optional<float> GetWakeNetThreshold() override { return 0.4f; }

    // Hardware-validated: this board has no echo cancellation (no
    // input_reference wiring), so its own TTS output can be picked back up
    // by the mic and misread as a fresh wake word right after an audio
    // channel closes. 1500ms suppresses that self-trigger loop while still
    // allowing a genuine "Hi ESP" shortly after a conversation ends.
    virtual uint32_t GetWakeWordCooldownAfterAudioCloseMs() override { return 1500; }
};

DECLARE_BOARD(FreenoveESP32S3Display);
