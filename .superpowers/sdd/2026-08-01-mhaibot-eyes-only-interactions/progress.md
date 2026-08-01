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

