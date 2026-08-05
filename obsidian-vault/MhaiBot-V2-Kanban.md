# MhaiBot V2 Kanban

## Done

- [x] Preserve approved baseline face changes.
- [x] Create isolated implementation worktree.
- [x] Add pure interaction model for petting, sleep text, groggy wake, and alert priority.
- [x] Enforce eyes-only chrome with black background and cyan eyes.
- [x] Add board-local face renderer with petting, sleep `Zzz`, and groggy wake animation.
- [x] Add touchscreen routing, 40-minute total idle screen-off, and five-second groggy wake.
- [x] Run host tests and ESP-IDF local build validation for `freenove-esp32s3-display-2.8-lcd`.
- [x] Generate local merged firmware artifact for commit `7d11943`.
- [x] Flash real hardware and fix a real bug found in the process (Listening state showed
      plain Neutral instead of the dedicated Listening pose — `application.cc` never actually
      requests a "listening" emotion; fixed board-locally in `mhaibot_display.cc`, commit `653aa84`).
- [x] Iterate the petting reaction to be visibly distinct on direct user feedback (thin
      near-closed "content" squint + glow, not just smaller eyes).
- [x] Add a startle gesture (triple-tap same spot) and reaction, plus a top-right color-emoji
      badge for petting/startled/groggy-wake reactions (commit `da90188`).
- [x] Force-pushed the local (OneDrive) implementation over PR #3's old branch, restored 3 CI
      workflow files the force-push had wiped, and confirmed all three CI checks pass on
      commit `90c6f28` (`Build MhaiBot Firmware`, `Build Boards`, `Claude Code Review`).
- [x] Root-caused and fixed the "reaction emoji only works for groggy-wake" issue — it was a
      stale build worktree (`C:\xmbot-v2-build` never re-synced after `653aa84`), not a code
      bug. Confirmed working for all three reactions after re-syncing and rebuilding; removed
      the diagnostic logging.

## Hardware Validation Pending

- [x] Flash and boot-verify on real hardware (multiple iterations this session, `COM4`).
- [x] Reaction emoji confirmed working for petting/startled/groggy-wake (see above).
- [x] All three GitHub CI checks green on latest commit (`Build MhaiBot Firmware`,
      `Build Boards` — full 178-board matrix, `Claude Code Review`).
- [x] Fixed a session-local `IDF_TARGET` env-leak from the unrelated C3 project (documented in
      the checklist); added `C:\xmbot-v2-build\build-s3.ps1` wrapper to prevent recurrence.
- [x] False-positive gesture rejection confirmed (tap / one-way stroke / vertical drift do not
      trigger petting).
- [x] Petting/startled confirmed not to toggle chat/listening on release.
- [x] First wake touch from screen-off confirmed consumed (observed live via a real sleep
      cycle during a long CI wait).
- [x] Speaker output confirmed by ear. Noted an unrelated wake-word timing quirk (first
      utterance right after "Hi ESP" sometimes not captured) — out of scope for this PR.
- [x] Groggy-wake ramp timing confirmed ~5s from log timestamps; flagged a minor
      brightness-restore anomaly (restores to a floor value instead of true pre-sleep
      brightness) for future investigation.
- [ ] In progress: 40-minute screen-off timing + `Zzz` sleep-text cycle + stability during
      sleep, via one continuous ~40-minute idle observation (board must not be touched).
- [ ] Wake word / urgent activity canceling groggy-wake immediately — needs precise timing,
      planned right after the screen-off test.
- [ ] Touch still responds after repeated sleep/wake cycles — planned right after.
- [ ] Error vs. low-battery alert priority — cannot be simulated remotely; step-by-step manual
      test instructions given to the user (Wi-Fi disconnect for error, real battery drain for
      low-battery) to run whenever convenient, non-blocking.
- [ ] The one unexplained reboot from before the stale-build issue was found — never
      reproduced on the correctly-synced build across extensive subsequent testing. Lower
      priority now but watch for recurrence.
- [ ] Keep PR/draft branch unmerged until every hardware checklist item is verified.

## Out of Scope

- [ ] Windows wrapper.
- [ ] Web Dashboard changes.
- [ ] TTS distortion fix.
- [ ] Audio, microphone, Wi-Fi, protocol, GPIO, sdkconfig, or camera behavior changes.
