# SDD ledger  plan: docs/plans/2026-08-01-mhaibot-eyes-only-interactions.md

## Task 1: Preserve Baseline and Establish Isolated Workspace

- Status: completed
- Started: 2026-08-02
- Completed: 2026-08-02
- Baseline branch: feature/mhaibot-face-v2-emotions
- Implementation branch: feature/mhaibot-face-v2-firmware
- Implementation worktree: C:/Users/Mhaiz/OneDrive/Documents/GitHub/xiaozhi-esp32/xiaozhi-esp32-mhaibot-v2-firmware
- Baseline patch: .superpowers/sdd/2026-08-01-mhaibot-eyes-only-interactions/baseline-approved-face.patch
- Safety baseline commit: 6f0d386
- Implementation agent: 019fbf26-e661-73e2-9d32-4d634e1462d6
- Spec reviewer: 019fbf27-cb5f-7f21-a7e6-28cb1c364d47
- Code-quality reviewer: 019fbf27-e2b1-7b40-a6dd-7b3510cd4a97
- Validation:
  - git status --short --branch
  - git worktree list
  - git apply --check baseline-approved-face.patch in a detached temporary worktree at 3877fce
- Notes:
  - Existing approved MhaiBot face changes in main/display/mhaibot_face.cc and main/display/mhaibot_face.h are preserved.
  - No prior SDD ledger existed; this ledger was created from the fallback path requested by the user.
  - The restored plan was moved from docs/ to docs/plans/ so the requested plan path and ledger first line match.
  - Reviewers found the first saved patch was malformed by PowerShell line handling; it was regenerated as a line-delimited patch and verified against the intended parent commit.

## Task 2: Pure interaction rules and timing model

- Status: completed
- Started: 2026-08-02
- Completed: 2026-08-02
- Commit: 60d0ccc
- Implementation agent: 019fbf29-ed55-7520-a210-9c73b2f37e5c
- Spec reviewers:
  - 019fbf2f-8e16-7ad2-92f1-08cb29522661 found skipped C++ runtime assertions were load-bearing.
  - 019fbf32-7a3b-7111-816f-f4c53ec76d3b approved after non-skipped spec/source checks were added.
- Code-quality reviewers:
  - 019fbf2f-a6ad-7a10-a520-3bb2d9f62722 reviewed the wrong checkout; result ignored.
  - 019fbf30-c38b-7250-921f-30dc3830f2dd approved the scoped model patch.
  - 019fbf32-90ec-7933-87ad-8b2b6e807fc0 requested clearer validation labeling.
  - 019fbf35-1804-7490-a370-a752bd345cc7 approved the corrected validation labels.
- Validation:
  - python -m unittest scripts.tests.test_mhaibot_face_model -v
  - python -m unittest discover -s scripts/tests -p 'test_*.py' -v
  - git diff --cached --check
- Validation notes:
  - ESP-IDF esp-clang compiled both C++ translation units with -Wall -Wextra -Werror.
  - Windows Application Control blocked ld.lld, so linked C++ runtime assertions were explicitly skipped locally.
  - Non-skipped Python spec/source-contract checks cover the required constants, timing, alert priority, and gesture examples without claiming to execute the C++ runtime.

## Task 3: Eyes-only chrome and minimal alert lifecycle

- Status: completed
- Started: 2026-08-02
- Completed: 2026-08-02
- Commit: 0a0e718
- Implementation agent: 019fbf36-d105-7de3-b5ed-945f27fd64fb
- Spec reviewers:
  - 019fbf3a-56d6-7072-81b6-7128b1f14b80 found the staged kLegacyEmotions compile break.
  - 019fbf3e-dc62-73a0-8516-54b32017605b found the localized Alert/Lang::Strings::ERROR lifecycle gap.
  - 019fbf3d-0ce5-7e21-951b-7506f6a45f25 approved the IsLegacyEmotion fix.
- Code-quality reviewers:
  - 019fbf3a-6e92-78d1-9451-128afa55b7b4 found the same staged kLegacyEmotions compile break.
  - 019fbf3e-f465-7ad3-af9b-2e8cfcd94382 found the same localized Alert lifecycle gap.
  - 019fbf41-e595-70f2-8e99-95a5844997a0 approved the localized alert lifecycle fix.
- Validation:
  - python -m unittest scripts.tests.test_mhaibot_face_model -v
  - python -m unittest discover -s scripts/tests -p 'test_*.py' -v
  - git diff --cached --check
  - python scripts/release.py freenove-esp32s3-display-2.8-lcd --name freenove-esp32s3-display-2.8-lcd
- Validation notes:
  - Focused and full Python suites passed with one explicit local skip for C++ runtime assertions because Windows Application Control blocks host ld.lld.
  - The release build failed in the OneDrive worktree during component-manager copy into managed_components before project C++ compile.
  - The same ESP-IDF v6.0.2 release build passed from a short detached validation worktree at C:/xmbot-v2-build for commit 0a0e718.
  - Firmware artifact from this validation build: C:/xmbot-v2-build/build/merged-binary.bin.

## Task 4: Petting, sleep text, and groggy face animation

- Status: completed
- Started: 2026-08-02
- Completed: 2026-08-02
- Commit: 2f7e521
- Implementation agent: 019fbf57-86cf-7bb2-9cb9-8d41178e991d
- Spec reviewer:
  - 019fbf5b-de42-7dc2-bb1f-0c75395df9fb approved the staged renderer task.
- Code-quality reviewers:
  - 019fbf5c-448b-7132-ae13-8ed575f8cc34 approved the staged renderer task.
  - 019fbf5e-7509-7ed1-8519-eba8a5d2c841 requested the no-mouth test cover both header and source; fixed before commit.
- Validation:
  - python -m unittest scripts.tests.test_mhaibot_face_model -v
  - python -m unittest discover -s scripts/tests -p 'test_*.py' -v
  - C:/Espressif/tools/esp-clang/esp-20.1.1_20250829/esp-clang/bin/clang-format.exe -i on touched board C++ files
  - git diff --cached --check
- Validation notes:
  - Focused and full Python suites passed with one explicit local skip for C++ runtime assertions because Windows Application Control blocks host ld.lld.
  - Shared main/display/mhaibot_face.* was not modified for this task; the Freenove board now uses board-local MhaiBotFaceV2.

