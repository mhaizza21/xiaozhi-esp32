# MhaiBot V2 Hardware Test Checklist

Firmware under test (updated on real hardware, 2026-08-03):

- Build/push commits: `653aa84` (Listening-pose fix) → `da90188` (petting/startle/reaction-emoji)
  → `256a30d` (docs) → `90c6f28` (CI restore, current).
- Built and flashed from the `C:\xmbot-v2-build` worktree (`idf.py build`).
- Board: Freenove ESP32-S3 Display 2.8" LCD, connected on `COM4` this session.
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

**Process note for next time:** `C:\xmbot-v2-build` is a separate git worktree from the main
project folder — editing source in the project folder does NOT automatically update it. After
committing `653aa84`, several rounds of "improve petting", "add startle gesture", "add reaction
emoji" edits were made directly to the project folder but the build worktree was never
re-synced (`git checkout <commit>`), so `idf.py build`/reflash cycles kept silently re-flashing
the same unchanged `653aa84` binary. What looked like a "petting still just gets smaller" or a
"reaction emoji works for groggy-wake but not petting/startled" bug was actually just repeatedly
testing old code — the real startle-tap gesture was never on the device at all during that
period (what read as a "startled" pose was the already-working double-tap → Listening pose).
Fixed by explicitly `git checkout 90c6f28` in the build worktree and deleting the stale `.obj`
files for the touched board sources before rebuilding. **Always verify the build worktree is on
the exact source commit before trusting a "the fix isn't working" observation.**

**Second process note:** this machine's Claude Code session is rooted in the separate
`xiaozhi-esp32` "main" worktree (used for the unrelated `mhaibot-servo-c3` ESP32-C3 project),
whose local `.vscode/settings.json` sets `IDF_TARGET=esp32c3`. That value leaks into every new
shell the session spawns regardless of working directory, which caused a `idf.py build` failure
mid-session ("sdkconfig was generated for esp32s3, but IDF_TARGET is esp32c3"). Editing that
other project's settings would break its own C3 workflow, so instead use
`C:\xmbot-v2-build\build-s3.ps1 <idf.py args>` (e.g. `build-s3.ps1 build`) for all MhaiBot builds
going forward — it explicitly forces `IDF_TARGET=esp32s3` before calling `idf.py`. Never call
`idf.py` directly from a bare PowerShell session for this project.

Artifact provenance: local ESP-IDF build; commit `90c6f28` is pushed to GitHub and passing all
three CI checks (`Build MhaiBot Firmware`, `Build Boards`, `Claude Code Review`).

## Verified on real hardware this session

- [x] Boots cleanly, connects to Wi-Fi and MQTT, reaches `idle` state (confirmed via serial log).
- [x] Idle shows only black background and two large cyan eyes (user-confirmed visually).
- [x] Listening now shows the dedicated wide-eyed Listening pose instead of plain Neutral
      (previously dead code — `Application::HandleStateChangedEvent()` always called
      `SetEmotion("neutral")` on entering Listening; fixed board-locally in `mhaibot_display.cc`).
- [x] Wi-Fi configuration screen still renders (eyes-only, no leftover chrome) when entered.
- [x] Valid petting gesture (3 direction reversals, mid-screen) triggers the improved thin,
      near-closed "content" eye squint with a soft glow shimmer.
- [x] Startle gesture (three quick taps at the same spot — the closest approximation of a
      "hard knock" this touch controller supports, since it has no pressure sensing) triggers
      the brief wide-eyed startled snap that relaxes back over ~700ms.
- [x] **Reaction emoji badge confirmed working for all three reactions** — petting (`loving`),
      startled (`shocked`), and groggy-wake (`sleepy`) — once tested against the correctly
      synced build. Live serial log during this test showed `ShowReactionEmoji` resolving a
      valid image pointer for both `'shocked'` and `'loving'`, and the user confirmed seeing
      the badge on screen.
- [x] Ran several minutes of continued normal use (idle, voice conversations, gesture testing)
      on the correctly-synced build with no crash, reboot, or watchdog reset.
- [x] Tap, one-way stroke, and excessive vertical drift correctly do NOT trigger petting
      (user confirmed all three behave the same as a plain tap — no petting reaction).
- [x] Petting/startled do not toggle chat/listening when the finger is released after the
      reaction finishes (user confirmed — nothing else happens once the eyes settle back).
- [x] First wake touch from screen-off is consumed — observed live: the board had gone to sleep
      during a long CI wait, and the user's first touch correctly triggered only the groggy-wake
      sequence (not a chat toggle); the `ShowReactionEmoji('sleepy')` badge and "eyes getting
      bigger" (Sleeping → Sleepy → Neutral) animation the user described match the intended
      wake ramp exactly.
- [x] Speaker output explicitly confirmed by ear — user heard a clear, normal spoken reply.
      Observation (not a new bug, and not touched by this PR — audio/wake-word pipeline is
      explicitly out of scope): saying "Hi ESP" alone woke the board but the immediately-following
      utterance wasn't captured/answered; a second, separate utterance got a normal reply. Likely
      a pre-existing wake-word timing quirk, not something introduced by this display/touch PR —
      worth a separate investigation later, but not blocking this one.

## Resolved — was a build/deploy process bug, not a firmware bug

- [x] ~~Reaction emoji does not show for petting or startled~~ — root-caused: the build
      worktree was stale (see process note above), not a code defect. Confirmed fixed by
      re-syncing the worktree and rebuilding; the diagnostic `ESP_LOGI` calls that were
      temporarily added to `ShowReactionEmoji()` have been removed now that this is closed.

## Still open

- [ ] **One unexplained reboot** was observed by the user while testing gestures on the *stale*
      build, before the worktree-sync issue was found. Never reproduced on the correctly-synced
      build across several minutes of testing, and no panic/backtrace was ever captured. Given
      the stale-build confusion explains most of that session's odd behavior, this is now lower
      priority, but still not conclusively explained — watch for recurrence.

## Not yet tested

- [ ] Full `Z`, `Zz`, `Zzz`, blank sleep text cycle above the eye.
- [ ] Screen stays on at 39 minutes idle / turns off at 40 minutes idle (not exercised — too
      long for this session; needs a dedicated long-running test).
- [ ] Groggy wake brightness ramp timing (~5s) and non-extension on repeated touch.
- [ ] Wake word / urgent activity cancels sleep/groggy state immediately.
- [ ] Error icon priority over low-battery icon; low-battery icon clears on recovery.
- [ ] No LVGL assertion / watchdog reset specifically during sleep/screen-off/wake cycles
      (only exercised during normal idle/petting/startled/conversation use, see above).
- [ ] Touch still responds after repeated sleep/wake cycles.
- [ ] Known TTS distortion — not re-tested this session; still assumed unresolved, not claimed
      fixed by this firmware.
