#include "eye_pixel_source_console.h"

#include "mhaibot_display.h"

#include <esp_console.h>
#include <esp_log.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr const char* TAG = "EyePixelSourceConsole";

int HandleEyePixelSourceCommand(void* context, int argc, char** argv) {
    auto* display = static_cast<MhaiBotDisplay*>(context);
    if (argc != 2) {
        printf("usage: eye_pixel_source <legacy|shadow|get>\n");
        return 1;
    }

    const char* arg = argv[1];
    if (std::strcmp(arg, "legacy") == 0) {
        display->SetEyePixelSource(MhaiBotFaceV2::PixelSource::kLegacy);
        printf("eye_pixel_source: legacy\n");
        ESP_LOGI(TAG, "pixel source set to kLegacy (engineer console)");
    } else if (std::strcmp(arg, "shadow") == 0) {
        display->SetEyePixelSource(MhaiBotFaceV2::PixelSource::kShadow);
        printf("eye_pixel_source: shadow\n");
        ESP_LOGW(TAG,
                 "pixel source set to kShadow (engineer console) - shadow rendering is not "
                 "yet hardware-validated (09 Slice 11B)");
    } else if (std::strcmp(arg, "get") == 0) {
        const bool is_shadow = display->GetEyePixelSource() == MhaiBotFaceV2::PixelSource::kShadow;
        printf("eye_pixel_source: %s\n", is_shadow ? "shadow" : "legacy");
    } else {
        printf("usage: eye_pixel_source <legacy|shadow|get>\n");
        return 1;
    }
    return 0;
}

}  // namespace

void RegisterEyePixelSourceConsole(MhaiBotDisplay* display) {
    static bool repl_started = false;
    if (!repl_started) {
        esp_console_repl_t* repl = nullptr;
        esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
        repl_config.prompt = "mhaibot>";
        esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
        ESP_ERROR_CHECK(esp_console_start_repl(repl));
        repl_started = true;
    }

    const esp_console_cmd_t cmd = {
        .command = "eye_pixel_source",
        .help = "Slice 11B engineer-only: get/set MhaiBotFaceV2 PixelSource "
                 "(legacy|shadow|get). Not exposed via MCP/voice.",
        .hint = nullptr,
        .func = nullptr,
        .argtable = nullptr,
        .func_w_context = HandleEyePixelSourceCommand,
        .context = display,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
