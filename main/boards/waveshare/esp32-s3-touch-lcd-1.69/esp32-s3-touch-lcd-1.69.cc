#include "application.h"
#include "button.h"
#include "codecs/dummy_audio_codec.h"
#include "config.h"
#include "display/lcd_display.h"
#include "mcp_server.h"
#include "mhaibot_sensor_hub.h"
#include "wifi_board.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_touch_cst816s.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

#define TAG "WsS3TouchLcd169"

class WaveshareEsp32s3TouchLcd169 : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    LcdDisplay* display_ = nullptr;
    MhaibotSensorHub* sensor_hub_ = nullptr;
    Button boot_button_;

    void InitializePowerControl() {
        gpio_config_t hold_cfg = {};
        hold_cfg.pin_bit_mask = 1ULL << PWR_HOLD_GPIO;
        hold_cfg.mode = GPIO_MODE_OUTPUT;
        ESP_ERROR_CHECK(gpio_config(&hold_cfg));
        gpio_set_level(PWR_HOLD_GPIO, 1);
    }

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {};
        i2c_bus_cfg.i2c_port = I2C_PORT;
        i2c_bus_cfg.sda_io_num = I2C_SDA_IO;
        i2c_bus_cfg.scl_io_num = I2C_SCL_IO;
        i2c_bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        i2c_bus_cfg.glitch_ignore_cnt = 7;
        i2c_bus_cfg.flags.enable_internal_pullup = 1;
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeSensors() {
        sensor_hub_ = new MhaibotSensorHub(i2c_bus_);
        sensor_hub_->InitializeMotion();
        sensor_hub_->InitializeRtc();
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGI(TAG, "Install ST7789 panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)DISPLAY_SPI_HOST, &io_config, &panel_io));

        ESP_LOGI(TAG, "Install ST7789 panel driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

        display_ = new SpiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeTouch() {
        gpio_config_t reset_cfg = {};
        reset_cfg.pin_bit_mask = 1ULL << TOUCH_RST_PIN;
        reset_cfg.mode = GPIO_MODE_OUTPUT;
        ESP_ERROR_CHECK(gpio_config(&reset_cfg));
        gpio_set_level(TOUCH_RST_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(TOUCH_RST_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(100));

        esp_lcd_touch_handle_t tp = nullptr;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH - 1,
            .y_max = DISPLAY_HEIGHT - 1,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = TOUCH_INT_PIN,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };

        esp_lcd_panel_io_handle_t tp_io_handle = nullptr;
        esp_lcd_panel_io_i2c_config_t tp_io_config = {};
        tp_io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
        tp_io_config.control_phase_bytes = 1;
        tp_io_config.lcd_cmd_bits = 8;
        tp_io_config.lcd_param_bits = 0;
        tp_io_config.flags.disable_control_phase = 1;
        tp_io_config.scl_speed_hz = 400 * 1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp));

        lvgl_port_touch_cfg_t touch_cfg = {};
        touch_cfg.disp = lv_display_get_default();
        touch_cfg.handle = tp;
        lvgl_port_add_touch(&touch_cfg);
        if (sensor_hub_) {
            sensor_hub_->SetTouchInitialized(true);
        }
        ESP_LOGI(TAG, "CST816T touch registered with LVGL");
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (sensor_hub_) {
                sensor_hub_->RecordTouchEvent("boot_button_click");
            }
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        mcp_server.AddTool("self.sensors.get_status",
            "Return Mhaibot sensor bring-up status for touch, IMU, RTC, and future PIR/camera capability placeholders.",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
                return sensor_hub_->GetStatusJson();
            });
        mcp_server.AddTool("self.sensors.get_motion",
            "Return one read-only QMI8658 accelerometer and gyroscope sample. This does not change robot behavior.",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
                return sensor_hub_->GetMotionJson();
            });
        mcp_server.AddTool("self.sensors.get_rtc_time",
            "Return the PCF85063 RTC time registers and clock integrity status. This does not set the clock.",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
                return sensor_hub_->GetRtcJson();
            });
        mcp_server.AddTool("self.sensors.get_touch_last_event",
            "Return board-level CST816T touch initialization state and the last board-observed touch event.",
            PropertyList(), [this](const PropertyList&) -> ReturnValue {
                return sensor_hub_->GetTouchJson();
            });
    }

public:
    WaveshareEsp32s3TouchLcd169() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializePowerControl();
        InitializeI2c();
        InitializeSensors();
        InitializeSpi();
        InitializeDisplay();
        InitializeTouch();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    AudioCodec* GetAudioCodec() override {
        static DummyAudioCodec audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE);
        return &audio_codec;
    }

    Display* GetDisplay() override {
        return display_;
    }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(WaveshareEsp32s3TouchLcd169);
