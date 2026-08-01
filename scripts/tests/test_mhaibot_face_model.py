import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD_DIR = ROOT / "main" / "boards" / "freenove-esp32s3-display-2.8-lcd"
MODEL_SOURCE = BOARD_DIR / "mhaibot_interaction_model.cc"
DISPLAY_HEADER = BOARD_DIR / "mhaibot_display.h"
DISPLAY_SOURCE = BOARD_DIR / "mhaibot_display.cc"
MODEL_TEST = ROOT / "scripts" / "tests" / "mhaibot_face_model_test.cc"


def find_cxx_compiler():
    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler:
        return compiler

    espressif_tools = Path("C:/Espressif/tools")
    if espressif_tools.exists():
        candidates = sorted(espressif_tools.glob("esp-clang/*/esp-clang/bin/clang++.exe"))
        if candidates:
            return str(candidates[-1])

    return None


class MhaiBotFaceModelTest(unittest.TestCase):
    def test_display_eyes_only_chrome_contract(self):
        header = DISPLAY_HEADER.read_text(encoding="utf-8")
        display_source = DISPLAY_SOURCE.read_text(encoding="utf-8")
        eyes_only_body = method_body(display_source, "void MhaiBotDisplay::ApplyEyesOnlyChrome()")

        self.assertNotIn("ApplyMinimalTheme", display_source)
        self.assertNotIn("kLegacyEmotions", display_source)
        self.assertIn("(void)emotion;", method_body(display_source, "bool MhaiBotDisplay::IsLegacyEmotion(const char* emotion)"))
        self.assertIn("lv_obj_t* alert_label_", header)
        self.assertIn("bool pending_error_status_", header)
        for helper in (
            "static bool IsErrorStatus(const char* status);",
            "static bool IsErrorEmotion(const char* emotion);",
            "static bool IsWarningEmotion(const char* emotion);",
            "static bool IsNotificationEmotion(const char* emotion);",
        ):
            self.assertIn(helper, header)
        for declaration in (
            "void SetStatus(const char* status) override;",
            "void ShowNotification(const char* notification, int duration_ms = 3000) override;",
            "void ShowNotification(const std::string& notification, int duration_ms = 3000) override;",
            "void SetChatMessage(const char* role, const char* content) override;",
            "void ClearChatMessages() override;",
            "void UpdateStatusBar(bool update_all = false) override;",
            "bool SetPanelPowered(bool powered);",
            "void StartPetting();",
            "void StartGroggyWake();",
            "void CancelTransientAnimation();",
        ):
            self.assertIn(declaration, header)

        for member in (
            "top_bar_",
            "status_bar_",
            "bottom_bar_",
            "low_battery_popup_",
            "notification_label_",
            "chat_message_label_",
        ):
            self.assertIn(member, eyes_only_body)
        self.assertIn("LV_OBJ_FLAG_HIDDEN", eyes_only_body)
        self.assertIn("lv_color_hex(kMhaiBotBackgroundColor)", eyes_only_body)

        for signature in (
            "void MhaiBotDisplay::SetTheme(Theme* theme)",
            "void MhaiBotDisplay::SetStatus(const char* status)",
            "void MhaiBotDisplay::SetChatMessage(const char* role, const char* content)",
            "void MhaiBotDisplay::ClearChatMessages()",
            "void MhaiBotDisplay::ShowNotification(const char* notification, int duration_ms)",
            "void MhaiBotDisplay::UpdateStatusBar(bool update_all)",
        ):
            self.assertIn("ApplyEyesOnlyChrome()", method_body(display_source, signature))

        self.assertIn("MhaiBotResolveAlert", display_source)
        self.assertIn("strcmp(status, Lang::Strings::ERROR) == 0", display_source)
        self.assertIn('strcmp(emotion, "cancel") == 0', display_source)
        self.assertIn('strcmp(emotion, "cloud_off") == 0', display_source)
        self.assertIn("pending_error_status_ && (IsErrorEmotion(emotion) || IsWarningEmotion(emotion))", display_source)
        self.assertIn("MATERIAL_SYMBOLS_BATTERY_ANDROID_0", display_source)
        self.assertIn("lv_color_hex(kMhaiBotErrorColor)", display_source)
        self.assertIn("lv_color_hex(kMhaiBotBatteryColor)", display_source)
        self.assertIn("lv_screen_active()", display_source)
        self.assertIn("lv_obj_align(alert_label_, LV_ALIGN_TOP_MID, 0, 12)", display_source)
        self.assertIn("esp_lcd_panel_disp_on_off(panel_", display_source)
        self.assertIn("ESP_ERR_NOT_SUPPORTED", display_source)
        self.assertIn('"Panel on/off is not supported; using backlight only"', display_source)

    def test_interaction_model_cpp_source_contract(self):
        """Checks the C++ source/test contract; this is not runtime execution."""
        source = MODEL_SOURCE.read_text(encoding="utf-8")
        cxx_test = MODEL_TEST.read_text(encoding="utf-8")

        for snippet in (
            "constexpr uint16_t kPetZoneTop = 32;",
            "constexpr uint16_t kPetZoneBottom = 104;",
            "constexpr uint16_t kPetMinStrokePx = 40;",
            "constexpr uint16_t kPetMaxVerticalDriftPx = 24;",
            "constexpr uint32_t kPetTimeoutMs = 2000;",
            "constexpr uint32_t kPetDurationMs = 3000;",
            "constexpr uint32_t kIdleSleepTimeoutSeconds = 600;",
            "constexpr uint32_t kScreenOffIdleSeconds = 2400;",
            "constexpr uint32_t kGroggyWakeDurationMs = 5000;",
            "constexpr uint8_t kGroggyMinBrightness = 20;",
            "if (!IsPetZoneY(y) || now_ms - started_ms_ > kPetTimeoutMs",
            "if (reversals_ >= 3)",
            "return MhaiBotAlert::kError;",
            "return MhaiBotAlert::kBatteryLow;",
        ):
            self.assertIn(snippet, source)

        for assertion in (
            'assert(MhaiBotSleepText(0) == "Z");',
            'assert(MhaiBotSleepText(500) == "Zz");',
            'assert(MhaiBotSleepText(1000) == "Zzz");',
            "assert(MhaiBotSleepText(1500).empty());",
            "assert(MhaiBotGroggyBrightness(2500, 75) == 47);",
            "assert(valid.Update(true, 20, 62, 1200));",
            "assert(!timeout.Update(true, 20, 60, 2400));",
            "assert(!leaves_zone.Update(true, 80, 110, 300));",
        ):
            self.assertIn(assertion, cxx_test)

    def test_interaction_model_python_spec_reference_not_cpp_runtime(self):
        """Documents the required behavior in Python; C++ runtime is checked separately."""
        self.assertEqual([sleep_text(t) for t in (0, 500, 1000, 1500, 2000)],
                         ["Z", "Zz", "Zzz", "", "Z"])
        self.assertEqual(groggy_progress(0), 0)
        self.assertEqual(groggy_progress(2500), 500)
        self.assertEqual(groggy_progress(5000), 1000)
        self.assertEqual(groggy_brightness(0, 75), 20)
        self.assertEqual(groggy_brightness(2500, 75), 47)
        self.assertEqual(groggy_brightness(5000, 75), 75)
        self.assertEqual(resolve_alert(False, False), "none")
        self.assertEqual(resolve_alert(False, True), "battery_low")
        self.assertEqual(resolve_alert(True, True), "error")

        valid = PetGesture()
        self.assertFalse(valid.update(True, 20, 60, 0))
        self.assertFalse(valid.update(True, 70, 62, 300))
        self.assertFalse(valid.update(True, 20, 61, 600))
        self.assertFalse(valid.update(True, 70, 63, 900))
        self.assertTrue(valid.update(True, 20, 62, 1200))

        timeout = PetGesture()
        self.assertFalse(timeout.update(True, 20, 60, 0))
        self.assertFalse(timeout.update(True, 70, 60, 700))
        self.assertFalse(timeout.update(True, 20, 60, 1400))
        self.assertFalse(timeout.update(True, 70, 60, 2100))
        self.assertFalse(timeout.update(True, 20, 60, 2400))

        leaves_zone = PetGesture()
        self.assertFalse(leaves_zone.update(True, 20, 100, 0))
        self.assertFalse(leaves_zone.update(True, 80, 110, 300))
        self.assertFalse(leaves_zone.update(True, 20, 100, 600))

    def test_display_eyes_only_chrome_and_alert_contract(self):
        """Checks the board-local display source contract; this is not runtime execution."""
        header = DISPLAY_HEADER.read_text(encoding="utf-8")
        source = DISPLAY_SOURCE.read_text(encoding="utf-8")

        for snippet in (
            '#include "mhaibot_interaction_model.h"',
            "void ApplyEyesOnlyChrome();",
            "lv_obj_t* alert_label_ = nullptr;",
            "void SetStatus(const char* status) override;",
            "void ShowNotification(const char* notification, int duration_ms = 3000) override;",
            "void ShowNotification(const std::string& notification, int duration_ms = 3000) override;",
            "void SetChatMessage(const char* role, const char* content) override;",
            "void ClearChatMessages() override;",
            "void UpdateStatusBar(bool update_all = false) override;",
            "bool SetPanelPowered(bool powered);",
            "void StartPetting();",
            "void StartGroggyWake();",
            "void CancelTransientAnimation();",
        ):
            self.assertIn(snippet, header + source)

        for snippet in (
            "MhaiBotResolveAlert(error_active_, battery_low_)",
            "pending_error_status_ = IsErrorStatus(status);",
            "pending_error_status_ && (IsErrorEmotion(emotion) || IsWarningEmotion(emotion))",
            'lv_label_set_text(alert_label_, "!");',
            "lv_obj_set_style_text_color(alert_label_, lv_color_hex(kMhaiBotErrorColor), 0);",
            "lv_label_set_text(alert_label_, MATERIAL_SYMBOLS_BATTERY_ANDROID_0);",
            "lv_obj_set_style_text_color(alert_label_, lv_color_hex(kMhaiBotBatteryColor), 0);",
            "lv_obj_align(alert_label_, LV_ALIGN_TOP_MID, 0, 12);",
            'strcmp(status, Lang::Strings::ERROR) == 0',
            'strcmp(emotion, "warning") == 0',
            'strcmp(emotion, "cancel") == 0',
            'strcmp(emotion, "cloud_off") == 0',
            'strcmp(emotion, "notification") == 0 || strcmp(emotion, "excited") == 0',
            "face_->SetEmotion(MhaiBotFace::Emotion::kHappy);",
            "esp_lcd_panel_disp_on_off(panel_, powered)",
            "err == ESP_ERR_NOT_SUPPORTED",
        ):
            self.assertIn(snippet, source)

        self.assertNotIn("ApplyMinimalTheme", header + source)
        self.assertNotIn("SpiLcdDisplay::ShowNotification(notification, duration_ms)", source)
        self.assertNotIn("SpiLcdDisplay::SetChatMessage(role, content)", source)
        self.assertNotIn("SpiLcdDisplay::ClearChatMessages()", source)

    def test_interaction_model_cpp_compile_and_runtime_assertions(self):
        """Compiles C++ with warnings-as-errors and runs assertions when linking is allowed."""
        compiler = find_cxx_compiler()
        if compiler is None:
            self.skipTest("g++ or clang++ is required for the host C++ model test")

        with tempfile.TemporaryDirectory() as directory:
            common_args = [
                compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(BOARD_DIR),
            ]
            subprocess.run(
                common_args
                + [
                    "-c",
                    str(MODEL_TEST),
                    "-o",
                    str(Path(directory) / "mhaibot_face_model_test.o"),
                ],
                check=True,
                cwd=ROOT,
            )
            subprocess.run(
                common_args
                + [
                    "-c",
                    str(MODEL_SOURCE),
                    "-o",
                    str(Path(directory) / "mhaibot_interaction_model.o"),
                ],
                check=True,
                cwd=ROOT,
            )

            output = Path(directory) / "mhaibot_face_model_test"
            command = common_args + [
                str(MODEL_TEST),
                str(MODEL_SOURCE),
                "-o",
                str(output),
            ]
            try:
                subprocess.run(command, check=True, cwd=ROOT, capture_output=True, text=True)
            except subprocess.CalledProcessError as error:
                output_text = f"{error.stdout}\n{error.stderr}"
                if "Application Control policy has blocked" in output_text:
                    self.skipTest(
                        "C++ runtime assertions were not executed: "
                        "host linker is blocked by Windows Application Control"
                    )
                raise
            subprocess.run([str(output)], check=True, cwd=ROOT)


def sleep_text(elapsed_ms):
    return ("Z", "Zz", "Zzz", "")[(elapsed_ms // 500) % 4]


def groggy_progress(elapsed_ms):
    return min(1000, (elapsed_ms * 1000) // 5000)


def groggy_brightness(elapsed_ms, target):
    target = max(20, target)
    return 20 + ((target - 20) * groggy_progress(elapsed_ms)) // 1000


def resolve_alert(error_active, battery_low):
    if error_active:
        return "error"
    if battery_low:
        return "battery_low"
    return "none"


class PetGesture:
    def __init__(self):
        self.reset()

    def reset(self):
        self.active = False
        self.start_y = 0
        self.leg_start_x = 0
        self.direction = 0
        self.reversals = 0
        self.started_ms = 0

    def update(self, touched, x, y, now_ms):
        if not touched:
            self.reset()
            return False
        if not self.active:
            if not 32 <= y <= 104:
                self.reset()
                return False
            self.active = True
            self.start_y = y
            self.leg_start_x = x
            self.direction = 0
            self.reversals = 0
            self.started_ms = now_ms
            return False
        if not 32 <= y <= 104 or now_ms - self.started_ms > 2000 or abs(y - self.start_y) > 24:
            self.reset()
            return False
        delta = x - self.leg_start_x
        if abs(delta) < 40:
            return False
        new_direction = 1 if delta > 0 else -1
        if self.direction and new_direction != self.direction:
            self.reversals += 1
            if self.reversals >= 3:
                self.reset()
                return True
        self.direction = new_direction
        self.leg_start_x = x
        return False


def method_body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1:index]
    raise AssertionError(f"method body not found for {signature}")


if __name__ == "__main__":
    unittest.main()
