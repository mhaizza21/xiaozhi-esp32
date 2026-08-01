# MhaiBot Eyes-Only Interactions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver Freenove ESP32-S3 firmware whose normal screen contains only expressive Cyan eyes, with minimal Error/Battery alerts, touchscreen petting, animated `Zzz`, 30-minute sleep screen-off, and a five-second groggy touch wake.

**Architecture:** Keep every behavior board-local under `freenove-esp32s3-display-2.8-lcd`. Put deterministic gesture/timing/priority rules in a pure C++ model tested on the host; let `MhaiBotFaceV2` render transients on its existing 33 ms LVGL timer; let the existing board `TouchTask` and `PowerSaveTimer` own touch routing, backlight, panel power, and wake priority without adding tasks or blocking delays.

**Tech Stack:** C++17, ESP-IDF 6.0.2, LVGL 9, ESP LCD panel API, existing FreeRTOS touch task, Python `unittest`, host `g++`, GitHub Actions.

## Global Constraints

- Board scope is only `freenove-esp32s3-display-2.8-lcd`; do not change shared display behavior for other boards.
- Normal UI is Cyan `#00C8E0` eyes on black `#000000`; hide time, Wi-Fi, normal battery, mute, status, subtitles, and chat text after Wi-Fi setup.
- Preserve the Wi-Fi configuration screen before connection and preserve hidden shared LVGL objects rather than deleting them.
- Error alert is `!` at top center `y=12`, color `#FF3B30`; low-battery alert uses the existing material battery icon at the same position, color `#FF8A00`; Error wins when both are active.
- Warning and Notification use Focused and Excited eyes respectively, with no alert icon.
- Petting is three horizontal direction reversals in `y=32..104`, each leg at least 40 px, vertical drift at most 24 px, completed within 2,000 ms; the response lasts 3,000 ms.
- Sleep begins after 600 seconds idle; `Z → Zz → Zzz → blank` advances every 500 ms at 45% Cyan opacity; screen-off occurs at 2,400 seconds total idle, exactly 1,800 seconds after sleep begins.
- A touch wake from screen-off starts at 20% backlight and ramps to saved brightness over 5,000 ms; its first touch is consumed and repeated touches neither stack nor extend the animation.
- Wake word, Error, or urgent activity cancels groggy wake and restores full wake immediately.
- Reuse the existing 33 ms face timer, existing `TouchTask`, and existing `PowerSaveTimer`; add no FreeRTOS task, polling task, blocking delay, camera, sensor, audio, Wi-Fi, pin, orientation, or Agent changes.
- Do not modify, stage, restore, or commit `main/assets/locales/nb-NO/err_reg.ogg`.
- The known TTS distortion is outside this work and must not be described as fixed.

---

### Task 1: Pure interaction rules and timing model

**Files:**
- Create: `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_interaction_model.h`
- Create: `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_interaction_model.cc`
- Modify: `scripts/tests/mhaibot_face_model_test.cc`
- Modify: `scripts/tests/test_mhaibot_face_model.py`

**Interfaces:**
- Consumes: raw touch samples `(touched, x, y, now_ms)` from the existing board `TouchTask`.
- Produces: `MhaiBotPetGestureDetector::Update(bool, uint16_t, uint16_t, uint32_t) -> bool`, `Reset()`, timing constants, `MhaiBotSleepText(uint32_t) -> std::string_view`, `MhaiBotGroggyProgressPerMille(uint32_t) -> uint16_t`, `MhaiBotGroggyBrightness(uint32_t, uint8_t) -> uint8_t`, and `MhaiBotResolveAlert(bool, bool) -> MhaiBotAlert`.

- [ ] **Step 1: Write failing pure C++ tests for exact constants, alert priority, sleep text, groggy progress, and brightness**

Add assertions to `scripts/tests/mhaibot_face_model_test.cc`:

```cpp
#include "mhaibot_interaction_model.h"

assert(MhaiBotPetZoneTop() == 32);
assert(MhaiBotPetZoneBottom() == 104);
assert(MhaiBotPetMinStrokePx() == 40);
assert(MhaiBotPetMaxVerticalDriftPx() == 24);
assert(MhaiBotPetTimeoutMs() == 2000);
assert(MhaiBotPetDurationMs() == 3000);
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
assert(MhaiBotGroggyBrightness(0, 75) == 20);
assert(MhaiBotGroggyBrightness(2500, 75) == 47);
assert(MhaiBotGroggyBrightness(5000, 75) == 75);
assert(MhaiBotResolveAlert(false, false) == MhaiBotAlert::kNone);
assert(MhaiBotResolveAlert(false, true) == MhaiBotAlert::kBatteryLow);
assert(MhaiBotResolveAlert(true, true) == MhaiBotAlert::kError);
```

- [ ] **Step 2: Write failing gesture tests for success and every false-positive case**

Use a helper that feeds samples into a new detector and assert:

```cpp
MhaiBotPetGestureDetector valid;
assert(!valid.Update(true, 20, 60, 0));
assert(!valid.Update(true, 70, 62, 300));
assert(!valid.Update(true, 20, 61, 600));
assert(!valid.Update(true, 70, 63, 900));
assert(valid.Update(true, 20, 62, 1200));

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
```

- [ ] **Step 3: Extend the host compile command and verify Red**

Add `mhaibot_interaction_model.cc` to the `g++` command in `test_mhaibot_face_model.py`, then run:

```bash
python -m unittest scripts.tests.test_mhaibot_face_model -v
```

Expected: FAIL because the new header/interfaces do not exist.

- [ ] **Step 4: Implement the minimal pure model**

Define the exact public surface in `mhaibot_interaction_model.h`:

```cpp
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
```

Implement `Update` so out-of-zone start, release, timeout, or excessive vertical drift calls `Reset`; a direction is committed only after displacement reaches 40 px; a change from committed `+1` to `-1` or vice versa increments `reversals_`; return true exactly on the third reversal and reset immediately. Clamp progress at 1,000 and use integer brightness interpolation from 20 to the saved value.

- [ ] **Step 5: Run the focused tests and full host suite**

```bash
python -m unittest scripts.tests.test_mhaibot_face_model -v
python -m unittest discover -s scripts/tests -p 'test_*.py' -v
```

Expected: all tests PASS; no compiler warning under `-Wall -Wextra -Werror`.

- [ ] **Step 6: Commit the pure model**

```bash
git add main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_interaction_model.h \
  main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_interaction_model.cc \
  scripts/tests/mhaibot_face_model_test.cc scripts/tests/test_mhaibot_face_model.py
git commit -m "feat(display): add MhaiBot interaction model"
```

### Task 2: Eyes-only chrome and minimal alert lifecycle

**Files:**
- Modify: `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_display.h`
- Modify: `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_display.cc`
- Modify: `scripts/tests/test_mhaibot_face_model.py`

**Interfaces:**
- Consumes: `MhaiBotAlert`, `MhaiBotResolveAlert`, shared display virtual methods, `Board::GetBatteryLevel`, and the theme large icon font.
- Produces: `ApplyEyesOnlyChrome()`, `SetAlertState(bool error_active, bool battery_low)`, `SetPanelPowered(bool)`, `StartPetting()`, `StartGroggyWake()`, and `CancelTransientAnimation()` on `MhaiBotDisplay`.

- [ ] **Step 1: Write failing source-contract tests for chrome persistence and display routing**

Add Python tests that extract method bodies and assert:

```python
for member in ("top_bar_", "status_bar_", "bottom_bar_", "low_battery_popup_"):
    self.assertIn(member, eyes_only_body)
self.assertIn("LV_OBJ_FLAG_HIDDEN", eyes_only_body)
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
self.assertIn("esp_lcd_panel_disp_on_off(panel_", display_source)
```

Also assert the old `ApplyMinimalTheme` name is absent so later updates cannot accidentally call the incomplete helper.

- [ ] **Step 2: Run the focused contract test to verify Red**

```bash
python -m unittest scripts.tests.test_mhaibot_face_model.MhaiBotFaceModelTest -v
```

Expected: FAIL because the overrides and alert/panel interfaces are absent.

- [ ] **Step 3: Add board-local overrides and alert state**

Declare these exact overrides in `mhaibot_display.h`:

```cpp
void SetStatus(const char* status) override;
void ShowNotification(const char* notification, int duration_ms = 3000) override;
void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
void SetChatMessage(const char* role, const char* content) override;
void ClearChatMessages() override;
void UpdateStatusBar(bool update_all = false) override;
bool SetPanelPowered(bool powered);
void StartPetting();
void StartGroggyWake();
void CancelTransientAnimation();
```

Add `alert_label_`, `error_active_`, and `battery_low_`. Rename `ApplyMinimalTheme()` to `ApplyEyesOnlyChrome()` and make it set `container_` and `content_` black while hiding `top_bar_`, `status_bar_`, `bottom_bar_`, `low_battery_popup_`, `notification_label_`, and chat content. Call the base implementation first in each override, then reapply chrome; the string `ShowNotification` overload forwards to the `const char*` overload.

- [ ] **Step 4: Create and update the single-position alert**

Create `alert_label_` as a child of `lv_display_get_screen_active(display_)` rather than the flex-layout `container_`, align `LV_ALIGN_TOP_MID` at `(0, 12)`, and keep it hidden for `kNone`. For `kError`, set text `!`, the active `LvglTheme` text font, and `lv_color_hex(0xFF3B30)`; for `kBatteryLow`, set `MATERIAL_SYMBOLS_BATTERY_ANDROID_0`, `static_cast<LvglTheme*>(current_theme_)->large_icon_font()->font()`, and `lv_color_hex(0xFF8A00)`. Always bring the label to the foreground after applying chrome.

In `SetEmotion`, set `error_active_` only for exact `error`, clear it for every subsequent non-error emotion, cancel transients for Error/Battery/conversation/sleep states, and preserve Warning→Focused and Notification→Excited with no icon. In `UpdateStatusBar`, call the base method, query `Board::GetBatteryLevel`, and set `battery_low_` when discharging and the shared level formula resolves to `MATERIAL_SYMBOLS_BATTERY_ANDROID_0`; then reapply chrome and alert priority.

- [ ] **Step 5: Implement panel power with a supported fallback**

Implement `SetPanelPowered` using:

```cpp
esp_err_t err = esp_lcd_panel_disp_on_off(panel_, powered);
if (err == ESP_OK) return true;
if (err == ESP_ERR_NOT_SUPPORTED) {
    ESP_LOGW(TAG, "Panel on/off is not supported; using backlight only");
    return false;
}
ESP_LOGE(TAG, "Panel on/off failed: %s", esp_err_to_name(err));
return false;
```

The caller must still turn the backlight off even when panel power is unsupported.

- [ ] **Step 6: Run tests and commit**

```bash
python -m unittest discover -s scripts/tests -p 'test_*.py' -v
git add main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_display.h \
  main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_display.cc \
  scripts/tests/test_mhaibot_face_model.py
git commit -m "feat(display): enforce eyes-only alerts"
```

Expected: full host suite PASS and only the one board-local alert position can become visible.

### Task 3: Petting, sleep text, and groggy face animation

**Files:**
- Modify: `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_face_v2.h`
- Modify: `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_face_v2.cc`
- Modify: `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_display.cc`
- Modify: `scripts/tests/test_mhaibot_face_model.py`

**Interfaces:**
- Consumes: `MhaiBotSleepText`, `MhaiBotGroggyProgressPerMille`, existing emotion poses, and the existing face timer.
- Produces: `MhaiBotFaceV2::StartPetting()`, `StartGroggyWake()`, `CancelTransientAnimation()`, and `IsGroggyWakeActive() const`.

- [ ] **Step 1: Write failing renderer contract tests**

Assert that `mhaibot_face_v2` contains exactly one sleep label, no second LVGL timer, and the three transient methods:

```python
self.assertIn("lv_obj_t* sleep_label_", header)
self.assertIn("void StartPetting()", header)
self.assertIn("void StartGroggyWake()", header)
self.assertIn("void CancelTransientAnimation()", header)
self.assertIn("MhaiBotSleepText(", source)
self.assertIn("MhaiBotGroggyProgressPerMille(", source)
self.assertEqual(source.count("lv_timer_create("), 1)
self.assertNotIn("vTaskDelay", source)
```

Verify the display wrapper methods delegate to the corresponding face methods.

- [ ] **Step 2: Run the focused test to verify Red**

```bash
python -m unittest scripts.tests.test_mhaibot_face_model.MhaiBotFaceModelTest -v
```

Expected: FAIL because transient rendering is not implemented.

- [ ] **Step 3: Add one transient state machine to the face renderer**

Add:

```cpp
enum class TransientMode { kNone, kPetting, kGroggyWake };
TransientMode transient_mode_ = TransientMode::kNone;
uint32_t transient_elapsed_ms_ = 0;
lv_obj_t* sleep_label_ = nullptr;
```

`StartPetting` saves the current target emotion, enters `kPetting`, and resets elapsed time. `StartGroggyWake` ignores duplicate calls while already groggy, enters `kGroggyWake`, and resets elapsed time. `CancelTransientAnimation` returns to the current requested core emotion without changing that requested emotion.

- [ ] **Step 4: Render the exact petting and groggy sequences in the existing `Tick()`**

For petting, interpolate from the current pose to a contented pose based on `kHappy` with eye height capped at 40 px and `eye_y += 8`; add a horizontal sway of `-2..+2` px using the existing frame counter; hold for 3,000 ms, then resume the saved emotion through the normal 300 ms transition.

For groggy wake, use `MhaiBotGroggyProgressPerMille`: interpolate Sleeping→Sleepy during progress `0..300`, then Sleepy→Neutral during `301..1000`; apply a slow blink envelope over the same 5,000 ms. At 5,000 ms clear the transient and begin the normal Neutral transition. Higher-priority `SetEmotion` calls invoke `CancelTransientAnimation` before their normal transition.

- [ ] **Step 5: Add the sleep-only `Zzz` object**

Create `sleep_label_` beneath the face root, position it above the viewer-right eye, set Cyan with `LV_OPA_40 + 13` (45% rounded to LVGL opacity), and hide it unless the resolved core emotion is Sleeping and there is no higher-priority transient. In `Tick`, set its text from `MhaiBotSleepText(sleep_elapsed_ms)`; reset the frame when leaving Sleeping.

- [ ] **Step 6: Run tests and commit**

```bash
python -m unittest discover -s scripts/tests -p 'test_*.py' -v
git add main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_face_v2.h \
  main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_face_v2.cc \
  main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_display.cc \
  scripts/tests/test_mhaibot_face_model.py
git commit -m "feat(display): animate MhaiBot touch and sleep"
```

Expected: full host suite PASS, one face timer only, and no blocking call in the renderer.

### Task 4: Touch routing, 40-minute screen-off, and five-second wake

**Files:**
- Modify: `main/boards/freenove-esp32s3-display-2.8-lcd/freenove-esp32s3-display-2.8-lcd.cc`
- Modify: `scripts/tests/test_mhaibot_face_model.py`

**Interfaces:**
- Consumes: `MhaiBotPetGestureDetector`, timing/brightness helpers, `MhaiBotDisplay` transient/panel methods, `Backlight::brightness`, existing `PowerSaveTimer`, and `Application::Schedule`.
- Produces: board state flags and helpers `BeginTouchWake()`, `FinishGroggyWake()`, `WakeImmediately()`, and `UpdateGroggyWake(uint32_t now_ms)`; consumes the first wake touch before legacy tap handling.

- [ ] **Step 1: Write failing board source-contract tests**

Assert the board source contains:

```python
self.assertIn("MhaiBotDisplay *display_", board_source)
self.assertIn("MhaiBotPetGestureDetector pet_gesture_", board_source)
self.assertIn("MhaiBotScreenOffIdleSeconds()", board_source)
self.assertIn("OnShutdownRequest", board_source)
self.assertIn("SetPanelPowered(false)", board_source)
self.assertIn("StartGroggyWake()", board_source)
self.assertIn("MhaiBotGroggyBrightness(", board_source)
self.assertIn("suppress_touch_release_", board_source)
self.assertEqual(board_source.count("xTaskCreatePinnedToCore("), 1)
```

Extract `TouchTask` and assert the suppression check occurs before `StartListening`, `ToggleChatState`, and `EnterWifiConfigMode` on release.

- [ ] **Step 2: Run the focused test to verify Red**

```bash
python -m unittest scripts.tests.test_mhaibot_face_model.MhaiBotFaceModelTest -v
```

Expected: FAIL because power/touch integration is absent.

- [ ] **Step 3: Add board-local state without another task**

Change `display_` to `MhaiBotDisplay*` and add:

```cpp
MhaiBotPetGestureDetector pet_gesture_;
bool screen_off_ = false;
bool touch_wake_pending_ = false;
bool groggy_wake_active_ = false;
bool suppress_touch_release_ = false;
uint32_t groggy_wake_started_ms_ = 0;
uint8_t groggy_target_brightness_ = 75;
uint8_t pre_sleep_brightness_ = 75;
```

Keep the existing single `TouchTask`; call `pet_gesture_.Update(t, x, y, now)` on every sample only while awake and not groggy. On recognition, schedule `display_->StartPetting()` and mark the current press consumed so its release cannot toggle chat.

- [ ] **Step 4: Configure sleep and idempotent screen-off**

Construct:

```cpp
power_save_timer_ = new PowerSaveTimer(
    -1, MhaiBotIdleSleepTimeoutSeconds(), MhaiBotScreenOffIdleSeconds());
```

On enter sleep, capture `GetBacklight()->brightness()` into `pre_sleep_brightness_` before dimming, cancel petting, set Sleeping, and dim to 8%. Register `OnShutdownRequest`; if `screen_off_` is false, set it true, cancel transients, set backlight to 0, and call `display_->SetPanelPowered(false)`. The guard is mandatory because `PowerSaveTimer` invokes shutdown on every later tick.

- [ ] **Step 5: Implement consumed touch wake and the brightness ramp**

On the first touch-down while `screen_off_`, set `touch_wake_pending_` and `suppress_touch_release_`, then call `WakeUp`. The exit-sleep callback checks `touch_wake_pending_`: clear screen-off, turn the panel on, set `groggy_target_brightness_ = std::max<uint8_t>(20, pre_sleep_brightness_)`, set brightness 20, call `StartGroggyWake`, set `groggy_wake_started_ms_`, and do not set Neutral yet.

Each existing 50 ms `TouchTask` iteration calls `UpdateGroggyWake(now)`, which uses `MhaiBotGroggyBrightness(now - groggy_wake_started_ms_, groggy_target_brightness_)`. At 5,000 ms it calls `RestoreBrightness`, sets Neutral, and clears the groggy flag. Repeated touch-down while groggy sets only `suppress_touch_release_`; it does not modify the start time.

- [ ] **Step 6: Preserve immediate non-touch wake priority**

In `SetPowerSaveLevel`, before `WakeUp`, mark the wake as immediate when level is not `LOW_POWER`. The exit callback then turns the panel on, clears groggy state, cancels transient animation, restores brightness, and lets the requested application emotion follow. Error and urgent paths receive the same immediate behavior through the existing non-low-power transition; they never wait five seconds.

- [ ] **Step 7: Run host tests and commit**

```bash
python -m unittest discover -s scripts/tests -p 'test_*.py' -v
git add main/boards/freenove-esp32s3-display-2.8-lcd/freenove-esp32s3-display-2.8-lcd.cc \
  scripts/tests/test_mhaibot_face_model.py
git commit -m "feat(board): add MhaiBot touch sleep wake flow"
```

Expected: all host tests PASS; the board still contains exactly one touch task creation.

### Task 5: Documentation, ESP-IDF build, artifact, and handoff

**Files:**
- Modify: `obsidian-vault/MhaiBot-V2.md`
- Modify: `obsidian-vault/MhaiBot-V2-Kanban.md`
- Modify: `obsidian-vault/MhaiBot-V2-Hardware-Test-Checklist.md`
- Modify: `docs/superpowers/specs/2026-08-01-mhaibot-eyes-only-interactions-design-th.md` only to record implementation/build evidence, without changing approved behavior.

**Interfaces:**
- Consumes: completed Tasks 1–4 and the existing targeted Freenove GitHub Actions workflow.
- Produces: reviewed branch/PR, ESP-IDF firmware artifact, SHA-256, Flash address `0x0`, and a board test checklist that explicitly leaves TTS distortion open.

- [ ] **Step 1: Run the complete local verification**

```bash
python -m unittest discover -s scripts/tests -p 'test_*.py' -v
git diff --check
git status --short
```

Expected: all tests PASS, `git diff --check` produces no output, and the only unrelated dirty path remains `main/assets/locales/nb-NO/err_reg.ogg`.

- [ ] **Step 2: Update Obsidian and hardware acceptance checks**

Record exact implemented timings/colors/gesture rules and add unchecked hardware items for: chrome absent in Idle/Listening/Thinking/Speaking; Error priority; low-battery lifecycle; valid and invalid pet gestures; full `Zzz` cycle; screen remains on at 39 minutes total idle and turns off at 40; first wake touch consumed; 5-second ramp; repeated touch not extending wake; wake word immediate; Wi-Fi configuration still visible; no LVGL assertion/watchdog reset; mic/speaker/touch regression; TTS distortion still unresolved.

- [ ] **Step 3: Commit documentation without the protected audio file**

```bash
git add obsidian-vault/MhaiBot-V2.md obsidian-vault/MhaiBot-V2-Kanban.md \
  obsidian-vault/MhaiBot-V2-Hardware-Test-Checklist.md \
  docs/superpowers/specs/2026-08-01-mhaibot-eyes-only-interactions-design-th.md
git diff --cached --name-only
git commit -m "docs(mhaibot): add interaction hardware checks"
```

Expected staged paths are exactly the four documentation files and never include `main/assets/locales/nb-NO/err_reg.ogg`.

- [ ] **Step 4: Push only the feature branch and run review/CI**

Publish the new commits to `feature/mhaibot-face-v2-firmware`, keep PR #3 Draft, run the targeted `Build Freenove ESP32-S3 Display 2.8 LCD` workflow with ESP-IDF 6.0.2, and request whole-branch review. If CI fails, use `superpowers:systematic-debugging` and `github:gh-fix-ci` before changing code.

Expected: host tests, ESP-IDF build, and review complete with no Critical/Important issue open.

- [ ] **Step 5: Download and verify the merged firmware**

Download the workflow artifact, extract `merged-binary.bin`, rename the distributable copy to include the short commit, and run:

```bash
sha256sum merged-binary.bin
stat -c '%s bytes' merged-binary.bin
```

Record the exact commit, byte size, SHA-256, workflow run, and Flash address `0x0` in Obsidian and the PR. Do not reuse the checksum of an older firmware.

- [ ] **Step 6: Hand off for board testing before merge**

Provide clickable BIN/ZIP/checklist links, instruct the user to flash the new merged BIN at `0x0`, and keep PR #3 Draft until the user verifies every hardware checklist item. State explicitly that the firmware changes display/touch/power behavior only and does not fix the separate TTS distortion.
