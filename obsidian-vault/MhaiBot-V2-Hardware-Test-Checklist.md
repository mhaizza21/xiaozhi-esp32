# MhaiBot V2 Hardware Test Checklist

Firmware under test (updated on real hardware, 2026-08-02):

- Build commits: `653aa84` (Listening-pose fix) → `da90188` (petting/startle/reaction-emoji)
- Built and flashed from the `C:\xmbot-v2-build` worktree (`idf.py build`)
- Board: Freenove ESP32-S3 Display 2.8" LCD, connected on `COM4` this session
- Flash method used: **individual pieces** (bootloader/partition-table/ota-data/app/assets at their
  own offsets), not a single merged image — this avoids re-erasing the NVS partition
  (`0x9000`–`0xd000`) and so preserves saved Wi-Fi credentials across reflashes:

```
esptool --chip esp32s3 --port COM4 -b 460800 write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m ^
  0x0 build\bootloader\bootloader.bin ^
  0x8000 build\partition_table\partition-table.bin ^
  0xd000 build\ota_data_initial.bin ^
  0x20000 build\xiaozhi.bin ^
  0x800000 build\generated_assets.bin
```

Artifact provenance: local ESP-IDF build only. Not yet pushed to GitHub / run through CI.

## Verified on real hardware this session

- [x] Boots cleanly, connects to Wi-Fi and MQTT, reaches `idle` state (confirmed via serial log).
- [x] Idle shows only black background and two large cyan eyes (user-confirmed visually).
- [x] Listening now shows the dedicated wide-eyed Listening pose instead of plain Neutral
      (previously dead code — `Application::HandleStateChangedEvent()` always called
      `SetEmotion("neutral")` on entering Listening; fixed board-locally in `mhaibot_display.cc`).
- [x] Wi-Fi configuration screen still renders (eyes-only, no leftover chrome) when entered.
- [x] Valid petting gesture (3 direction reversals, mid-screen) triggers a visibly distinct
      thin, near-closed "content" eye squint with a soft glow shimmer (iterated twice on
      direct user feedback — first version read as just "eyes got smaller").
- [x] New startle gesture — three quick taps at the same spot (closest approximation of a
      "hard knock" this touch controller supports; it has no pressure sensing) — triggers a
      brief wide-eyed startled snap that relaxes back over ~700ms. User-confirmed pose change.
- [x] Reaction emoji badge (top-right, real color emoji from the bundled
      `noto-color-emoji_64` collection) correctly shows during groggy wake-up (`sleepy`).
- [x] Ran ~4 minutes of continued normal use (idle + a full voice conversation) after the
      reaction-emoji change with no crash, reboot, or watchdog reset.

## Known open issue — do not re-flag as new

- [ ] **Reaction emoji does not show for petting or startled**, even though the eye-pose change
      for both works correctly and both call the exact same `ShowReactionEmoji()` path as the
      working groggy-wake case. Not yet root-caused. Diagnostic `ESP_LOGI` calls were left in
      `MhaiBotDisplay::ShowReactionEmoji()` to speed up the next debugging session.
- [ ] **One unexplained reboot** was observed by the user while testing gestures shortly after
      the reaction-emoji feature was first flashed. Could not reproduce it again over ~4 minutes
      of subsequent testing (including a full voice conversation) or find a panic/backtrace in
      the serial log — but it was not conclusively ruled out as related to the emoji change
      either. Treat as an open risk until reproduced and root-caused.

## Not yet tested

- [ ] Tap / one-way-stroke / out-of-zone / excessive-vertical-drift / timeout correctly do NOT
      trigger petting (only the valid-gesture case was exercised).
- [ ] Petting/startled do not toggle chat/listening on release.
- [ ] Full `Z`, `Zz`, `Zzz`, blank sleep text cycle above the eye.
- [ ] Screen stays on at 39 minutes idle / turns off at 40 minutes idle (not exercised — too
      long for this session; needs a dedicated long-running test).
- [ ] First wake touch from screen-off is consumed (not toggling chat/listening/Wi-Fi config).
- [ ] Groggy wake brightness ramp timing (~5s) and non-extension on repeated touch.
- [ ] Wake word / urgent activity cancels sleep/groggy state immediately.
- [ ] Error icon priority over low-battery icon; low-battery icon clears on recovery.
- [ ] No LVGL assertion / watchdog reset specifically during sleep/screen-off/wake cycles
      (only exercised during normal idle/petting/startled/conversation use, see above).
- [ ] Touch still responds after repeated sleep/wake cycles.
- [ ] Speaker output explicitly confirmed by ear (a voice reply was logged as text and the
      conversation completed normally, which implies the audio path worked, but no one
      explicitly confirmed hearing it).
- [ ] Known TTS distortion — not re-tested this session; still assumed unresolved, not claimed
      fixed by this firmware.
