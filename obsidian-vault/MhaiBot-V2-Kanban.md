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

## Hardware Validation Pending

- [x] Flash and boot-verify on real hardware (multiple iterations this session, `COM4`).
- [ ] Root-cause why the reaction emoji shows for groggy-wake but not petting/startled
      (diagnostic logging left in `ShowReactionEmoji()`).
- [ ] Reproduce/rule out one unexplained reboot observed during gesture testing.
- [ ] Complete the remaining unchecked items in
      `obsidian-vault/MhaiBot-V2-Hardware-Test-Checklist.md` (40-minute screen-off timing,
      false-positive gesture rejection, alert priority, speaker heard by ear, etc.).
- [ ] Push this branch to GitHub and run it through real CI (host tests + ESP-IDF build +
      board matrix + review) — everything so far is local-only verification.
- [ ] Keep PR/draft branch unmerged until every hardware checklist item is verified.

## Out of Scope

- [ ] Windows wrapper.
- [ ] Web Dashboard changes.
- [ ] TTS distortion fix.
- [ ] Audio, microphone, Wi-Fi, protocol, GPIO, sdkconfig, or camera behavior changes.
