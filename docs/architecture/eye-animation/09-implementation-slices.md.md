# 09. Implementation Slices

**Status:** Draft  
**Purpose:** Break the single roadmap (`06` §11 / `04` / `08`) into
small, independently reviewable PRs for the Freenove extract from
`MhaiBotFaceV2`.  
**Sources of truth:** `05-state-machine.md`, `06-rendering-pipeline.md`,
`07-animation-spec.md`, and `ADR.md` (ADR-001…006). If this document
disagrees with those, update this document.

## Constraints

- **Planning only** in this file; production work happens in later PRs.
- **Board-local** under
  `main/boards/freenove-esp32s3-display-2.8-lcd/` until **ADR-006**.
  Do not move into `main/display/eye/`.
- Prefer **extraction / adapter** over rewrite.
- Preserve existing `MhaiBotFaceV2` visible behavior until the explicit
  **behavior-parity gate** (Slice 10) and the final **delete-legacy**
  slice (Slice 12).
- Each slice stays **independently reviewable / mergeable**, ideally
  **≤ ~500 lines changed**, and must **compile independently** on top of
  prior merged slices.
- Honor: DeviceState adapter (**ADR-002**), ownership (**ADR-003**),
  blink openness multiplier (**ADR-004**), mutex mailbox (**ADR-005**).
- No Transition Engine, Animation Scheduler, or Render Queue (**ADR-001**).
- **No slice** may delete or bypass `ResolveRenderedPose` /
  `InterpolatePose`, or **stop dual-feeding live inputs into legacy**,
  until Slice 12’s deletion gate. Mailbox may become the **primary
  pixel path** in Slice 11, but legacy must keep receiving identical
  live inputs so `pixel_source=legacy` can reconstruct pixels
  immediately (no app restart).
- Cutover-related slices must document a **Rollback strategy**.

## Baseline paths (today)

| Role | Path |
|---|---|
| Face (extraction baseline) | `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_face_v2.{h,cc}` (~96 / ~384 LOC) |
| Display / emotion routing | `.../mhaibot_display.{h,cc}` (~72 / ~459 LOC) |
| Touch / sleep helpers | `.../mhaibot_interaction_model.{h,cc}` |
| Board power / sleep entry | `.../freenove-esp32s3-display-2.8-lcd.cc` |
| Board source GLOB | `main/CMakeLists.txt` — `boards/${BOARD_DIR}/*.cc` only (top-level) |

**Build note:** New `.cc` files under an `eye/` subdirectory are **not**
picked up by the current GLOB. Slices that introduce `eye/*.cc` must
either (a) place sources at the board root (`eye_*.cc`), or (b) add a
narrow Freenove-safe append of `boards/${BOARD_DIR}/eye/*.cc` in
`main/CMakeLists.txt`. Prefer (b) once with Slice 0 so later slices can
use the `eye/` layout from `01`.

Suggested new tree (illustrative; match `01`):

```text
main/boards/freenove-esp32s3-display-2.8-lcd/
├── mhaibot_face_v2.h / .cc
├── mhaibot_display.h / .cc
├── mhaibot_interaction_model.*
├── freenove-esp32s3-display-2.8-lcd.cc
└── eye/
    ├── eye_activity.h          # enums from 05 (or shared header)
    ├── eye_intent.h
    ├── eye_frame.h
    ├── eye_intent_mailbox.h / .cc
    ├── eye_activity_adapter.h / .cc
    ├── eye_animator.h / .cc
    ├── emotion_controller.h / .cc
    ├── blink_controller.h / .cc
    ├── idle_controller.h / .cc
    ├── eye_animation_coordinator.h / .cc
    └── lvgl_eye_renderer.h / .cc
```

Host-side geometry tests (optional per slice) may live under a board-local
or `scripts/tests` path; prefer pure C++ helpers with no LVGL.

------------------------------------------------------------------------

## Legacy behavior inventory (FaceV2)

Scan of `MhaiBotFaceV2` (`ResolveRenderedPose`, `InterpolatePose`,
`Tick`, entry APIs) plus related board/display overlays. Everything in
**Migrate** must land in coordinator/animator **before** Slice 12 may
delete or bypass the legacy path. Items marked **Keep outside animator**
stay on face shell / display / board per **ADR-003**.

| ID | Behavior | Source (today) | Destination ownership | Notes |
|---|---|---|---|---|
| T1 | **Petting** pose: lerp `current_pose_` → `PettingPose()` over `transition_ms`; frame sway/bob | `ResolveRenderedPose` + `PettingPose` | Coordinator `EyeTransient` + animator | Duration `MhaiBotPetDurationMs()`; end → `BeginTransitionTo(target_emotion_)` |
| T2 | **Petting** opacity triangle-wave shimmer | `Tick` → `ApplyEyeOpacity` | Animator opacity on `EyeFrame` (or face opacity apply fed by animator) | Only while petting; else `LV_OPA_COVER` |
| T3 | **Startled** pose: snap `StartledPose()` then lerp to `ResolveBasePose(target_emotion_)` over startle window | `ResolveRenderedPose` + `StartledPose` | Coordinator + animator | Duration `MhaiBotStartleDurationMs()`; end → transition to target |
| T4 | **GroggyWake** pose: sleeping→sleepy (0–300‰) then sleepy→neutral (300–1000‰); height pseudo-blink clamp | `GroggyPose` via `ResolveRenderedPose` | Coordinator + animator | Duration `MhaiBotGroggyWakeDurationMs()`; end → Neutral; re-entry no-op if already groggy |
| T5 | **Steady-state emotion transition** lerp `transition_from_` → base pose over `transition_ms` | `ResolveRenderedPose` + `InterpolatePose` + `BeginTransitionTo` | Animator (emotion transition) | Triggered by `SetEmotion` / post-transient resume; `SetEmotion` cancels active transient |
| T6 | **CancelTransientAnimation** clears mode + hides sleep label | Face API | Coordinator clear + face shell `HideSleepLabel` | Board/display call sites must keep working |
| T7 | **IsGroggyWakeActive** query | Face | Coordinator/face thin wrapper | Board uses this during wake |
| T8 | **Sleep Zzz label** cycle while sleeping and no transient | `Tick` + `ApplySleepLabel` / `HideSleepLabel` / `MhaiBotSleepText` | Face shell owns LVGL label (**ADR-003**); coordinator/activity only signals *when* sleeping | Not pose math; must remain correct after cutover |
| T9 | **Sleeping / Sleepy / Listening / … base poses** | `ResolveBasePose` | `EmotionController` (+ activity tweaks in Slice 7) | Extract numbers; no restyle |
| T10 | Entry wrappers `StartPetting` / `StartStartled` / `StartGroggyWake` | Face + `MhaiBotDisplay` | Keep board-facing APIs; forward to coordinator after migration | Do not remove in Slice 12 |
| B1 | Pet / startle **gesture detectors** | `mhaibot_interaction_model` + board | Stay board | Out of animator scope |
| B2 | Groggy **backlight brightness** curve | Board `StartGroggyWake` / `MhaiBotGroggyBrightness` | Stay board (**ADR-003**) | Face owns eye geometry only |
| B3 | Reaction **emoji** overlays (loving/shocked/sleepy) | `MhaiBotDisplay::ShowReactionEmoji` | Stay display shell | Parallel to eye geometry |
| B4 | Sleep **entry / cancel** + panel power policy | Board | Stay board | Publishes sleeping/wake cues only |

**Migration completeness gate (hard):** Slice 12 must not delete
`ResolveRenderedPose` / `InterpolatePose` until T1–T7 and T9 are
behaviorally equivalent on the new path, T8 still correct under face
ownership, and T10 entry points still work. B1–B4 remain outside the
animator by design.

------------------------------------------------------------------------

## Dual-path mailbox / cutover sequence

Safe cutover. **Primary pixel path** ≠ **stop feeding legacy**.

| Step | Meaning | Owning slice(s) |
|---|---|---|
| **A** | Add mailbox without changing behavior | Slice 2 |
| **B** | Publish intents to **both** legacy and new paths | Slice 3 (start), Slice 9 (complete dual feed) |
| **C** | Run new path in **shadow** mode (compute, do not drive pixels) | Slice 9 |
| **D** | Compare **canonical** frames (same stage; see Slice 9 Option A) | Slice 9 |
| **E** | Pass host + hardware **parity gates** | Slice 10 |
| **F** | Enable new path behind a **feature flag** (pixels from new path) | Slice 11 |
| **G** | Validate **immediate rollback** to legacy (legacy still live-fed) | Slice 11 |
| **H** | Make mailbox / new path the **primary pixel path**; **keep dual-feeding** identical live inputs into legacy | Slice 11 (after F/G + short hardware soak) |
| **I** | Stop legacy publication and remove legacy path **only** in final cleanup | Slice 12 |

**Ownership until Slice 12:** display/board producers dual-feed every
intent (emotion, activity, transient entry) into **both** mailbox and
legacy `SetEmotion` / `Start*` APIs. Slice 11 may flip which path drives
`Render`; it must **not** stop legacy publication.

**Rollback trigger:** runtime `pixel_source=legacy` (preferred). Must
work mid-session without app restart while dual-feed is alive.

**Rollback verification:** Slice 11 hardware drill flips flag mid-session
and confirms correct legacy pixels (legacy still receiving live inputs).

**Deletion gate:** only Slice 12 may stop dual-feed / delete
`ResolveRenderedPose` / `InterpolatePose`, and only after post-cutover
validation and explicit retirement of the live rollback switch.

Until Step **F** (or while flag=`legacy`), legacy remains pixel
authority. After **F**/**H**, new path may drive pixels, but dual-feed
continues so rollback stays immediate.

------------------------------------------------------------------------

## Dependency order

```text
Slice 0  (EyeFrame + Pose adapter + eye/ GLOB)
    → Slice 1  (LVGLEyeRenderer dual-path; legacy pose still authority)
    → Slice 2  (Animator + mailbox skeleton — cutover A)
    → Slice 3  (EyeIntent types + adapter tables; dual-publish start — B)
    → Slice 4  (BlinkController)
    → Slice 5  (IdleController)
    → Slice 6  (EmotionController base poses)
    → Slice 7  (Activity behaviors + coordinator priority)
    → Slice 8  (Legacy transient migration: pet / startle / groggy / …)
    → Slice 9  (Shadow dual-path compare — C/D; Option A canonical stage)
    → Slice 10 (Behavior-parity gate — E; hard stop)
    → Slice 11 (Flag enable → primary path; dual-feed continues — F/G/H)
    → Slice 12 (Stop dual-feed; delete legacy resolvers — I)
```

Roadmap alignment (`06` §11):

| §11 step | Slices |
|---|---|
| 1–2 Inspect / ownership | Covered by PR notes in Slice 0 (and ADR-003); no separate code PR required |
| 3 Introduce `EyeFrame` | Slice 0 |
| 4 Renderer adapter | Slice 1 |
| 5 Animator (static / pass-through) | Slice 2 |
| 6 Blink | Slice 4 |
| 7 Idle | Slice 5 |
| 8 Emotion mapping | Slices 3, 6, 7, 11 |
| Transient extract | Slice 8 (required before delete) |
| Dual-path / cutover | Slices 9, 11 (dual-feed through 11; teardown in 12) |
| Parity gate | Slice 10 (Option A canonical compare) |
| 9 Remove legacy | Slice 12 (after Slices 8 + 10 + 11; stops dual-feed) |

------------------------------------------------------------------------

## Slice 0 — Safest extraction: `EyeFrame` + Pose adapter

### Goal

Introduce stereo `EyeGeometry` / `EyeFrame` and a lossless
`MhaiBotFaceV2::Pose` ↔ `EyeFrame` adapter with **zero visible change**.

### Scope

- Add board-local headers matching `06` / `02` (no openness field on
  `EyeFrame` — **ADR-004**).
- Implement `PoseToEyeFrame` / `EyeFrameToPose` (or equivalent) from
  current `Pose` fields (`left_x`, `right_x`, `y`, `width`, `height`,
  `radius`); rotation defaults to 0; opacity defaults to 1 unless an
  existing opacity path is mirrored.
- Optionally call the adapter in a debug/assert dual-check beside
  `ApplyPose` without changing LVGL write order.
- Enable `eye/*.cc` GLOB if using the `eye/` directory.
- PR description must restate ADR-003 ownership (`sleep_label_`,
  Show/Hide, backlight stay outside the future renderer).

### Files expected to change

- `main/boards/freenove-esp32s3-display-2.8-lcd/eye/eye_frame.h` (new)
- `main/boards/freenove-esp32s3-display-2.8-lcd/eye/eye_pose_adapter.h` (new; optional `.cc`)
- `main/boards/freenove-esp32s3-display-2.8-lcd/mhaibot_face_v2.cc` (minimal include / dual-check)
- `main/CMakeLists.txt` (only if adding `eye/*.cc` GLOB append)

### Public API affected

- None external to the board. New internal types only.

### Risks

- Integer `Pose` ↔ float `EyeGeometry` rounding drift if round-tripped
  carelessly — convert once in one direction for the dual-check.
- Accidental CMake GLOB that pulls unrelated trees — keep append
  board-dir-scoped.

### Test strategy

- Host unit tests: round-trip representative poses (neutral, sleeping
  height, asymmetric left/right x) with exact or ±0.5 px policy
  documented.
- Freenove build succeeds; no hardware behavior expectation change.

### Completion criteria

- Types match `06`; build green for Freenove variant.
- Face still uses existing `ApplyPose` path as the authority.
- No pixel / timing change on device smoke (or dual-path asserts hold).
- Slice compiles and merges independently.

### Estimated PR size

~150–250 LOC (mostly new headers + small adapter + optional CMake).

------------------------------------------------------------------------

## Slice 1 — `LVGLEyeRenderer` dual-path

### Goal

Extract LVGL eye property updates into `LVGLEyeRenderer` while frames
still come from legacy `ResolveRenderedPose()` → `PoseToEyeFrame`.

### Scope

- Implement `LVGLEyeRenderer::Init(left, right)` / `Render(frame)` /
  `Deinit` per `02`.
- Move logic equivalent to `ApplyPose` + eye opacity apply into the
  renderer; clamp / reject non-finite values (`06` §5 / §10).
- Face shell retains object lifetime for `root_`, eyes, `sleep_label_`
  (**ADR-003**). Renderer binds refs only; does **not** own
  `sleep_label_` or Show/Hide.
- Tick path: `ResolveRenderedPose` → frame → `Render`; delete or thin
  inline `ApplyPose` body to a call into the renderer.
- **Do not** remove `ResolveRenderedPose` / `InterpolatePose` or change
  transient ownership.

### Files expected to change

- `.../eye/lvgl_eye_renderer.h` / `.cc` (new)
- `.../mhaibot_face_v2.h` / `.cc` (wire renderer; keep sleep label /
  timer / transients)

### Public API affected

- None to `Display` / application. Internal face tick uses renderer.

### Risks

- Opacity path divergence between eyes and sleep chrome.
- Forgetting display lock assumptions — stay on existing `lv_timer` /
  display-locked callers only.

### Test strategy

- Side-by-side visual smoke: Idle neutral, Listening, Sleeping Zzz,
  petting/startle if available — must match pre-slice.
- Build Freenove; optional host test that clamps NaN/Inf before “render”
  into a fake sink.

### Completion criteria

- All left/right eye LVGL mutations go through `Render(EyeFrame)`.
- `sleep_label_`, Show/Hide, backlight unchanged.
- Visible parity with Slice 0 baseline.
- Legacy pose resolution still authoritative.

### Estimated PR size

~200–350 LOC.

------------------------------------------------------------------------

## Slice 2 — `EyeAnimator` pass-through + mailbox skeleton (cutover A)

### Goal

Introduce LVGL-free `EyeAnimator::Update(delta_ms)` that initially
**pass-through** or emits static/neutral frames equivalent to current
targets, plus the **ADR-005** mutex mailbox type **without** changing
pixel authority (cutover step **A**).

### Scope

- `EyeAnimator` with `Reset` / `Update` / `frame()`; no LVGL includes.
- Coordinator shell optional but thin: can live inside face tick as
  “consume optional mailbox else use legacy-derived frame”.
- Add `EyeIntentMailbox` (`Publish` / `ConsumeLatest`) per **ADR-005**.
- Do **not** yet replace `SetEmotion` / transient pose ownership; legacy
  still computes the geometric target.
- Prefer extracting `InterpolatePose` / lerp helpers into animator as
  shared math **copies or callees**, not deleting face copies yet.
- Mailbox may be constructed and optionally consumed with
  “empty → ignore”; pixels still from legacy pose → frame → Render.

### Files expected to change

- `.../eye/eye_animator.h` / `.cc` (new)
- `.../eye/eye_intent.h` (new; struct only if not already added)
- `.../eye/eye_intent_mailbox.h` / `.cc` (new)
- `.../mhaibot_face_v2.h` / `.cc` (tick wiring; legacy authority)

### Public API affected

- Internal only. Mailbox API exists but display may not Publish yet.

### Risks

- Double interpolation (legacy + animator) — choose pass-through **or**
  move lerp wholly into animator in this slice, not both.
- Mailbox unused dead code — acceptable if wired into tick consume with
  “no value → legacy intent path”.

### Rollback strategy

- No runtime flag required: mailbox is unused for pixels. Revert PR /
  leave mailbox unconsumed. Legacy path unchanged.

### Test strategy

- Host tests: animator pass-through equals input frame; lerp matches
  extracted `LerpInt` for fixed seeds.
- Device: no visible change vs Slice 1.

### Completion criteria

- Animator can emit `EyeFrame`; renderer remains dumb.
- Mailbox compiles and is safe to Publish from non-LVGL tasks later.
- Behavior parity preserved; legacy `ResolveRenderedPose` still drives
  pixels.
- Slice compiles/merges independently.

### Estimated PR size

~250–400 LOC.

------------------------------------------------------------------------

## Slice 3 — `EyeIntent` enums + `EyeActivityAdapter` tables (dual-publish start, B)

### Goal

Add activity/emotion enums and the **ADR-002** mapping tables in one
board-local adapter, with tests, and optionally **shadow-publish**
intents **in parallel** with legacy `face_->SetEmotion(...)` without
cutting display off the legacy path (cutover step **B** start).

### Scope

- `EyeActivity` / `EyeEmotion` (and `EyeTransient` enum if needed) per
  `05`.
- `EyeActivityAdapter` implementing `02` / ADR-002 tables
  (`DeviceState` → activity, emotion strings → `EyeEmotion`, ordered
  overrides, `blink_allowed` defaults).
- Display publishes intent into the mailbox **in parallel** with
  existing `face_->SetEmotion(...)` (shadow publish); face **ignores
  mailbox for geometry** (legacy still authoritative).
- Do not change backlight / sleep ownership (**ADR-003**).
- Do **not** stop dual-feed or make mailbox the pixel path.

### Files expected to change

- `.../eye/eye_activity.h` (or enums in `eye_intent.h`) (new)
- `.../eye/eye_activity_adapter.h` / `.cc` (new)
- `.../mhaibot_display.cc` (optional shadow Publish only)
- Host tests for mapping tables (recommended)

### Public API affected

- None required. Shadow publish is internal.

### Risks

- Mapping drift vs `ToFaceEmotion` / `listening_status_active_` — tests
  must lock Listening+neutral and Connecting→Idle cases from ADR-002.
- Premature primary-path cutover or stopping dual-feed — explicitly
  out of scope here.

### Rollback strategy

- Stop shadow `Publish` calls; leave mailbox unused. Legacy
  `SetEmotion` path unchanged. Revert is display-only.

### Test strategy

- Table-driven host tests for every `DeviceState` and emotion string in
  `02`.
- Compare adapter output to current `ToFaceEmotion` expectations for
  overlapping cases.

### Completion criteria

- Adapter is the single documented mapping implementation.
- Legacy display→face emotion path still authoritative for pixels.
- Dual-publish (when enabled) does not change visuals.
- Tests green; Freenove build green; independently mergeable.

### Estimated PR size

~200–350 LOC (tables + tests).

------------------------------------------------------------------------

## Slice 4 — Blink (`BlinkController`)

### Goal

Add auto-blink / double-blink via openness **multiplier** composed in a
**shared post-compose stage** (**ADR-004**), with suppression rules from
`05` §8 / `07` §5.

### Scope

- `BlinkController`: timing, `SetAllowed`, `Trigger`,
  `openness_multiplier()`.
- Apply multiplier to height in a **shared post-compose** step that runs
  **after** the canonical pose/`EyeFrame` (emotion / activity /
  pet-startle-groggy / transition) and **before** `Render` — not only
  inside the new-path animator’s private emit.
- **Ownership:** one shared composition stage owns blink (and later
  idle). Both legacy-primary and new-primary pixel paths feed the same
  stage so visible blink is path-independent.
- **No** `openness` field on `EyeFrame`.
- Suppress during Sleeping / Waking / Error / Booting and when
  `blink_allowed` is false.
- Legacy face has no separate blink today — this is a **deliberate
  additive** behavior; keep intensity/timing within `07` ranges so it
  does not fight sleep/groggy height hacks.
- Do not move sleep height ownership into blink.
- Do not delete legacy transient paths.
- Canonical (pre-blink) frames remain the Slice 9 compare inputs — see
  Slice 9 Option A.

### Files expected to change

- `.../eye/blink_controller.h` / `.cc` (new)
- `.../eye/eye_animator.*` and/or a small shared post-compose helper
- `.../eye/eye_animation_coordinator.*` (if introduced; else face tick)
- Possibly small `mhaibot_face_v2.cc` wiring

### Public API affected

- Optional face/coordinator `TriggerBlink` — board-local only.

### Risks

- Double-close with Sleepy/Sleeping base height × blink — rely on
  suppression (**ADR-004** risk note).
- Watchdog if blink update does heavy work — keep Update O(1).
- Applying blink only on the new path would poison Slice 9 parity —
  shared post-compose is mandatory for Option A.

### Test strategy

- Host: openness curve over time; suppression when disallowed.
- Hardware: Idle shows blink; Sleeping/Waking do not auto-blink;
  no jumps/flicker.

### Completion criteria

- Blink works on Idle; suppressed on sleep/wake/error/boot.
- `EyeFrame` still has no openness member.
- Blink is applied in the shared post-compose stage, not baked into
  legacy `ResolveRenderedPose`.
- Legacy emotion/transient paths still present and authoritative for
  canonical (pre-blink) geometry by default.

### Estimated PR size

~250–400 LOC.

------------------------------------------------------------------------

## Slice 5 — Idle (`IdleController`)

### Goal

Add event-driven micro gaze / glance (`07` §6) without competing with
Listening / Thinking / Speaking (`07` §7), via the same **shared
post-compose** stage as blink.

### Scope

- `IdleController`: `SetEnabled`, offsets, event-based (not per-frame
  RNG).
- Coordinator enables idle only when activity/priority allow (`05`).
- Compose gaze offsets in shared post-compose after emotion/activity/
  transient canonical frame, before blink multiplier (`07` §9).
- **Ownership:** shared with blink post-compose (Slice 4); not a
  new-path-only additive that would fail Slice 9 compare.
- Do not delete legacy transient paths.
- Idle offsets are **excluded** from Slice 9 canonical compare (Option A).

### Files expected to change

- `.../eye/idle_controller.h` / `.cc` (new)
- Shared post-compose / `eye_animator.*` / coordinator
- Minimal face wiring

### Public API affected

- None external; optional intent `look_x` / `look_y` already on
  `EyeIntent`.

### Risks

- Motion sickness / busy eyes if amplitude too high — stay in `07`
  ranges.
- Fighting Listening pose — disable idle under interaction activities.
- New-path-only idle would create false Slice 9 mismatches — keep shared.

### Test strategy

- Host: seeded event schedule produces stable offset sequence.
- Hardware: Idle has subtle life; Listening/Speaking stay steady.

### Completion criteria

- Idle motion only when allowed; no per-frame `rand`.
- Idle runs in shared post-compose; canonical frames remain comparable.
- Parity elsewhere unchanged; independently mergeable.

### Estimated PR size

~200–350 LOC.

------------------------------------------------------------------------

## Slice 6 — `EmotionController` base poses

### Goal

Extract emotion base geometry from `ResolveBasePose` /
`MhaiBotFaceV2::Emotion` into `EmotionController` driven by
`EyeEmotion`, still fed by legacy emotion until Slice 11.

### Scope

- Map `05` / `07` emotions: Neutral, Happy, Sad, Angry, Surprised,
  Focused, Sleepy (and activity-neutral baselines); bridge shipping
  Freenove emotions including Thinking/Speaking/Listening/Sleeping/
  Relaxed/Confident/Robot2 as needed for parity (inventory **T9**).
- Bridge: existing `MhaiBotFaceV2::Emotion` → `EyeEmotion` at the face
  edge so pixels stay aligned during migration.
- Animator uses emotion contribution as base pose; **legacy transient
  overlays (pet/startle/groggy) remain on the face shell** until
  Slice 8.
- Keep `ResolveBasePose` available until Slice 12 (may call through to
  controller).

### Files expected to change

- `.../eye/emotion_controller.h` / `.cc` (new)
- `.../eye/eye_animator.*`
- `.../mhaibot_face_v2.cc` (call controller instead of inline pose
  tables where safe)

### Public API affected

- Internal. `SetEmotion` on face may still accept legacy enum.

### Risks

- Pose table mismatch vs shipping face — extract numbers, don’t restyle.
- Partial migration leaving two pose tables — delete duplicate only in
  Slice 12 after parity.

### Test strategy

- Host: each `EyeEmotion` emits expected width/height/radius band.
- Hardware: cycle emotions via existing `SetEmotion` strings; compare to
  pre-slice screenshots / eye feel.

### Completion criteria

- Emotion base poses come from `EmotionController` (or face wrappers
  calling it).
- Visible match to prior Freenove emotions within `07` tolerance.
- Legacy transient APIs still work via `ResolveRenderedPose`.

### Estimated PR size

~300–450 LOC.

------------------------------------------------------------------------

## Slice 7 — Activity behaviors + coordinator priority

### Goal

Encode activity adjustments and `05` §7 priority in
`EyeAnimationCoordinator`, including Sleeping / Waking / Listening /
Speaking / Thinking / Error / Booting behaviors that are still partially
embedded in face tick / display flags.

### Scope

- Coordinator applies priority: Error > Sleep/Wake > interaction >
  Speaking/Listening/Thinking > emotion transition > idle.
- Activity geometry tweaks from `07` (e.g. Listening attention) live
  here or in animator composition — not in the renderer.
- Face shell keeps `sleep_label_` updates when activity is Sleeping
  (**ADR-003**; inventory **T8**).
- Accept `EyeTransient` hooks but **do not** claim pet/startle/groggy
  parity until Slice 8 completes — entry points may forward to legacy
  until then.
- Do not stop dual-feed; do not delete `ResolveRenderedPose`.
  Mailbox is not yet the pixel path.

### Files expected to change

- `.../eye/eye_animation_coordinator.h` / `.cc` (new or complete)
- `.../eye/eye_animator.*`
- `.../mhaibot_face_v2.cc` (tick delegates to coordinator where safe)
- Possibly read-only references from `mhaibot_display.cc` / board for
  sleep flags (no power policy move)

### Public API affected

- Board-local coordinator `SetIntent` / `SetTransient` / `Update`.

### Risks

- Priority bugs (idle during Listening, blink during wake).
- Sleep label desync if activity flag and board sleep diverge — board
  remains caller of sleeping emotion + brightness (**ADR-003**).

### Rollback strategy

- Keep face tick able to fall back to legacy `ResolveRenderedPose` for
  geometry. Coordinator can be bypassed with a compile-time or runtime
  flag defaulting to legacy-primary.

### Test strategy

- Host: priority matrix unit tests (Error preempts idle; Speaking keeps
  activity while emotion storms).
- Hardware: sleep → groggy wake → idle; listening status + neutral
  emotion; error interrupt — legacy transients still via face APIs.

### Completion criteria

- Coordinator is the policy owner for composition inputs on the **new**
  path.
- Renderer still geometry-only.
- Legacy path still available and still drives pixels by default.
- Independently mergeable.

### Estimated PR size

~300–450 LOC.

------------------------------------------------------------------------

## Slice 8 — Legacy transient migration (pet / startle / groggy / …)

### Goal

Migrate **all** FaceV2 transient and transition geometry from
`ResolveRenderedPose` / `InterpolatePose` / related `Tick` overlays
into the coordinator/animator **before** any slice deletes or bypasses
the legacy path. Covers inventory **T1–T7** and preserves **T8/T10**
entry/shell contracts; does not move **B1–B4**.

### Scope

Exact legacy behavior to preserve (extract, do not restyle):

1. **Petting (T1/T2):** lerp into thin Happy-derived squint
   (`height=10`, soft radius, `y+=12`); sway/bob; opacity shimmer;
   duration `MhaiBotPetDurationMs()`; end resumes emotion transition to
   `target_emotion_`; `HideSleepLabel` on start.
2. **Startled (T3):** immediate wide `StartledPose` (widen/raise from
   current target base), then lerp to `ResolveBasePose(target_emotion_)`
   over `MhaiBotStartleDurationMs()`; end resumes target transition.
3. **GroggyWake (T4):** two-segment lerp Sleeping→Sleepy→Neutral with
   `MhaiBotGroggyProgressPerMille`; height clamp pseudo-blink; duration
   `MhaiBotGroggyWakeDurationMs()`; end → Neutral; ignore re-entry while
   active; face geometry only (brightness stays board **B2**).
4. **Steady-state transition (T5):** `transition_from_` → base over
   `transition_ms` via same per-mille lerp; `SetEmotion` cancels
   transient then begins transition.
5. **Cancel / IsGroggy (T6/T7):** clear transient; query active groggy.
6. **Sleep label (T8):** still face-owned; show only when sleeping and
   no transient — coordinator must expose enough state for face tick.

Source → destination:

| Source | Destination |
|---|---|
| `ResolveRenderedPose` transient branches | Coordinator transient state + animator composition |
| `InterpolatePose` / `LerpInt` / `ClampProgress` | Animator shared lerp helpers |
| `PettingPose` / `StartledPose` / `GroggyPose` | Animator/controller transient pose builders |
| Pet opacity shimmer in `Tick` | Animator `EyeFrame.opacity` (or explicit opacity out-param) |
| `Start*` / `Cancel*` face APIs | Thin wrappers → coordinator (keep signatures) |

**During this slice:** new path implements transients; **pixels remain
legacy-primary** unless an explicit shadow-compare harness is local and
off by default. Prefer dual implementation (legacy + new) until Slice 9
compare and Slice 10 parity pass.

### Files expected to change

- `.../eye/eye_animation_coordinator.*`
- `.../eye/eye_animator.*`
- Possibly `.../eye/emotion_controller.*` (shared base poses for groggy
  segments)
- `.../mhaibot_face_v2.h` / `.cc` (forward `Start*` / `Cancel*` /
  `IsGroggyWakeActive`; keep legacy functions intact)
- Host parity tests (new)

### Public API affected

- Prefer stable board-facing methods (`StartPetting`, `StartStartled`,
  `StartGroggyWake`, `CancelTransientAnimation`,
  `IsGroggyWakeActive`). Internal ownership moves toward coordinator.

### Risks

- Partial migration leaving one transient on legacy and another on new
  path — inventory checklist in PR must mark T1–T7 done.
- Opacity shimmer forgotten (T2) while pose migrates (T1).
- Groggy brightness (B2) accidentally pulled into animator.

### Rollback strategy

- Runtime or compile flag: `use_legacy_transients=true` (default)
  keeps `ResolveRenderedPose` branches authoritative.
- New transient code may run in shadow only. Revert PR restores face-only
  behavior with no mailbox dependency.

### Test strategy (parity tests)

- Host golden tests with fixed seeds/timelines for:
  - Petting pose + opacity samples across duration
  - Startle snap + relax curve
  - Groggy progress segments + height clamp
  - Emotion transition lerp
  - Cancel mid-pet; SetEmotion cancels transient
  - Sleep label eligibility: sleeping + no transient vs transient active
- Compare new-path **canonical** `EyeFrame` (via adapter) to legacy
  `Pose`/`PoseToEyeFrame` at the same stage (Slice 9 Option A:
  before shared blink/idle) within documented ±0.5 px (or exact int)
  policy.
- Hardware smoke still on legacy-primary path.

### Completion criteria

- Inventory T1–T7 implemented on coordinator/animator with host parity
  tests green.
- T8 still correct under face ownership; T10 entry points still work.
- B1–B4 unchanged in ownership.
- **`ResolveRenderedPose` / `InterpolatePose` still present** and still
  the default pixel path.
- Explicit checklist in PR: every inventory ID addressed.
- Independently mergeable; ≤~500 LOC preferred (split only if forced,
  keeping pet/startle/groggy in the first migration PR).

### Estimated PR size

~350–500 LOC (extract + tests; split follow-up only if forced).

------------------------------------------------------------------------

## Slice 9 — Shadow dual-path compare (cutover C/D)

### Goal

Keep the legacy path as the **active pixel authority** while running the
new coordinator/animator path in **shadow mode**, publishing to both
paths, and comparing outputs **at the same pipeline stage** before any
primary-path cutover (steps **B** complete, **C**, **D**).

### Chosen comparison model: Option A (canonical pre-compose)

**Why Option A (not B):** FaceV2 has no separate blink/idle controllers
today; Slices 4–5 add them as deliberate additive behavior. Comparing
final pixels (new path with blink/idle vs `ResolveRenderedPose` without)
would create **false mismatches**. Option B (both through identical
post-compose) is valid but heavier and unnecessary once both paths
share one post-compose stage. Option A is safer and fairer here.

| Field | Definition |
|---|---|
| **Comparison point** | **Canonical `EyeFrame`** after emotion / activity / migrated pet-startle-groggy / steady transition, **before** shared blink/idle post-compose |
| **Ownership** | Legacy: `ResolveRenderedPose` → `PoseToEyeFrame`. New: animator/coordinator emit of the same stage. Shared post-compose (Slices 4–5) owns blink/idle once, applied only to the active pixel path after compare |
| **Why valid** | Both systems produce the same stage of geometry under identical dual-fed inputs; additive blink/idle are not in the diff |
| **Acceptable exclusions** | Blink openness multiplier; idle gaze/glance offsets; post-compose clamps that exist solely for those additives. **Not** excluded: pet/startle/groggy/transition/opacity shimmer (T1–T5) — those must match at the canonical stage |

### Scope

- Ensure every display/board intent producer dual-feeds:
  legacy `SetEmotion` / transient entry **and** mailbox/`SetTransient`.
- Each tick: compute legacy canonical frame **and** new-path canonical
  frame; apply shared blink/idle only to the **rendered** (legacy)
  path; do **not** drive pixels from shadow.
- Compare canonical geometry (and opacity when petting) with
  counters/logs for mismatches; optional debug assert behind a flag.
- **Do not** compare post-blink / post-idle frames against bare
  `ResolveRenderedPose` — that is the unfair mismatch this slice forbids.
- No stop of dual-feed; no deletion of legacy resolvers.
- Feature flag scaffolding for Slice 11 may be added here **defaulting
  to legacy-primary / shadow-on**.

### Files expected to change

- `.../mhaibot_face_v2.h` / `.cc` (shadow tick compare at canonical stage)
- `.../mhaibot_display.h` / `.cc` (complete dual-publish)
- `.../eye/eye_animation_coordinator.*` / animator (shadow Update;
  expose pre-compose frame)
- Optional board-local debug counters / host compare harness

### Public API affected

- None at app level. Internal dual-feed only.

### Risks

- Shadow path CPU cost on device — keep compare O(1) field diffs; disable
  verbose logging by default.
- False mismatches from float rounding — reuse Slice 0 policy.
- Accidental compare-after-blink — gate tests must assert comparison
  uses pre-compose frames only.

### Rollback strategy

- Flags: `shadow_compare=off` stops new-path Update; `pixel_source=legacy`
  (default). Immediate rollback = leave flags at defaults or revert PR.
- Legacy path never removed in this slice; dual-feed stays on.

### Test strategy

- Host: recorded timelines (idle, emotion storm, pet, startle, groggy,
  sleep label window) produce zero **canonical** mismatches under policy;
  blink/idle may still run on the rendered path without failing compare.
- Hardware: run with shadow on; confirm no visible change vs pre-slice
  beyond intentional shared blink/idle; collect mismatch counters == 0
  on checklist scenarios.

### Completion criteria

- Dual-publish complete for steady-state and transient entry points.
- Shadow compare runs at Option A comparison point; mismatch budget met
  (prefer zero); false blink/idle mismatch risk removed.
- Legacy still sole pixel source.
- Ready for Slice 10 parity gate.
- Independently mergeable.

### Estimated PR size

~250–400 LOC.

------------------------------------------------------------------------

## Slice 10 — Behavior-parity gate (hard stop, cutover E)

### Goal

Prove the extracted pipeline matches `07` on hardware (and host tests)
**and** that migrated transients match legacy at the **Option A
canonical stage**, **before** enabling primary-path cutover (Slice 11)
or deleting legacy (Slice 12).

### Scope

- No feature work. Validation + bugfix PRs only.
- Execute `07` §12 / `06` §12 / `08` validation checklist **plus**
  inventory T1–T8 hardware checks.
- Confirm shadow compare (Slice 9 Option A) stayed clean on device —
  canonical mismatches only; blink/idle must not pollute the gate.
- Document residual gaps; fix blockers in small hotfixes that land
  **before** Slice 11.
- Confirm ADR compliance: mapping, ownership, openness model, mailbox,
  still board-local.
- Confirm dual-feed still active (prerequisite for Slice 11 rollback).

### Files expected to change

- Ideally none, or tiny bugfix under `eye/*` + face/display only.
- Optional checklist note in PR description / issue — **do not** edit
  `05`/`06`/`07` unless a true SoT bug is found.

### Public API affected

- None.

### Risks

- Shipping cutover/delete with known parity holes — this gate exists to
  prevent that.
- “Good enough” fatigue — require explicit sign-off on the checklist.
- Treating blink/idle pixel diffs as parity failures — reject; gate uses
  Option A.

### Rollback strategy

- N/A for validation-only; any hotfix must preserve legacy-primary
  default and dual-feed. Do not merge Slice 11 without go/no-go.

### Test strategy (gate checklist)

- [ ] Emotions: Neutral, Happy, Sad, Angry, Surprised, Focused, Sleepy
      (+ Freenove bridges as shipped)
- [ ] Activities: Idle, Listening, Thinking, Speaking, Sleeping, Waking,
      Booting, Error interrupt
- [ ] Blink present on Idle; suppressed on Sleeping/Waking
- [ ] Idle motion subtle; off during Listening/Speaking as specified
- [ ] **Pet / startle / groggy** match legacy feel and timing (T1–T4)
- [ ] Emotion transitions and cancel mid-transient (T5–T6)
- [ ] Sleep Zzz label still correct; hidden during transients (T8)
- [ ] Shadow **canonical** compare counters acceptable on hardware
      (Option A; blink/idle excluded from mismatch)
- [ ] No one-frame jumps, clipping, flicker; no watchdog; no heap growth
- [ ] SPI frame interval acceptable; status/alert/preview paths OK
- [ ] Host geometry / adapter / blink / idle / **transient parity**
      tests green
- [ ] Inventory T1–T7 + T9 signed off as migrated-equivalent
- [ ] Dual-feed still publishing identical live inputs to legacy

### Completion criteria

- Checklist signed off (human hardware review).
- Known Sev-1/Sev-2 visual or stability issues closed.
- Explicit go/no-go for Slice 11 recorded in the PR/issue.
- Explicit statement that Slice 12 delete is **still blocked** until
  Slice 11 post-cutover validation **and** retirement of live rollback.

### Estimated PR size

0–150 LOC (fixes only); primarily review/QA.

------------------------------------------------------------------------

## Slice 11 — Flag enable → primary path; dual-feed continues (cutover F/G/H)

### Goal

After Slice 10 go/no-go: enable the new path behind a flag, validate
**immediate** rollback, then make mailbox/new path the **primary pixel
path** — **without** stopping dual-feed into legacy and **without**
deleting `ResolveRenderedPose` / `InterpolatePose`.

### Scope

- **F:** Flag `pixel_source=new` (name illustrative) drives
  `Render` from coordinator/animator canonical frame → shared
  blink/idle post-compose.
- **G:** Confirm flipping flag back to `legacy` restores prior pixels
  **immediately, mid-session, without app restart** (runtime flag
  required for this guarantee). Legacy must still be receiving
  identical live inputs so `ResolveRenderedPose` is not stale.
- **H:** After short hardware soak with flag on, treat mailbox/new path
  as the **primary pixel path** (flag may default to `new` after soak
  sign-off). Display/board **must continue** dual-feeding identical
  live inputs into legacy `SetEmotion` / `Start*` / related entry
  points. **Do not** stop legacy publication in this slice.
- Preserve Show/Hide, alert emoji, preview image paths (**ADR-003**).
- Speaking: activity from `DeviceState`; emotion from LLM strings.
- Sleeping / groggy overrides per ADR-002 ordered list; transient entry
  still via display/board helpers → both mailbox and legacy.

#### Rollback contract (required documentation in PR)

| Item | Requirement |
|---|---|
| **Ownership** | Display/board dual-feed producers keep legacy live; face tick selects pixel source via flag; shared post-compose applies blink/idle to whichever path renders |
| **Rollback trigger** | Runtime `pixel_source=legacy` (or equivalent). Must not require reflash/restart while dual-feed is alive |
| **Rollback verification** | Hardware drill in this slice: flip to legacy mid-session during soak; confirm correct, current emotion/transient pixels |
| **Deletion gate** | Dual-feed stop + legacy resolver deletion deferred to Slice 12 only |

### Files expected to change

- `.../mhaibot_display.h` / `.cc`
- `.../mhaibot_face_v2.h` / `.cc` (flagged pixel source; mailbox consume;
  **keep** legacy tick inputs live)
- `.../eye/eye_activity_adapter.*` (wire-up only if needed)
- `.../freenove-esp32s3-display-2.8-lcd.cc` only if sleep/groggy must
  publish intent explicitly (prefer going through display APIs)
- Small Kconfig / constexpr / runtime flag as chosen in PR

### Public API affected

- `Display::SetEmotion` / `SetStatus` behavior unchanged at app level;
  internal routing changes (dual-feed remains).
- Face public emotion enum API may become a thin bridge (document in PR).

### Risks

- Missed publish paths (petting, startled, preview hide) leaving stale
  intent on **either** path — both must stay current.
- Stopping dual-feed early (false “sole intent source”) — **hard forbid**;
  that breaks immediate rollback.
- Cross-task Publish without Schedule for complex board actions —
  follow ADR-005 (Publish snapshot; Schedule for LVGL-adjacent work).
- Flag default wrong in production builds — default **legacy** until soak
  signed off, then flip default in a tiny follow-up if needed.

### Rollback strategy

- **Required:** runtime `pixel_source=legacy` remains available through
  post-cutover hardware validation and until Slice 12 deletion gate.
- On regression: set flag to legacy immediately (no restart); file
  hotfix against new path; do **not** proceed to Slice 12.
- Dual-feed is what makes immediate rollback correct; do not remove it
  here.

### Test strategy

- Host: adapter + mailbox integration (latest-wins under concurrent
  publishes if simulated).
- Hardware with flag **on**: boot → idle → listen → speak → emotion
  changes → sleep/wake → pet/startle/groggy → error alert chrome.
- **Hardware rollback drill (blocking):** with dual-feed still on, flip
  `pixel_source=legacy` mid-session **without restart**; confirm
  pixels match current intent (not a stale last-legacy frame); flip
  back to `new` and continue soak.
- Abbreviated re-run of Slice 10 checklist after primary-path enable.

### Completion criteria

- New path drives pixels behind flag; soak signed off.
- Rollback drill demonstrated: immediate, no restart, correct pixels.
- Mailbox/new path is **primary pixel path** when flag=`new` (**H**);
  legacy still dual-fed with identical live inputs.
- Legacy `ResolveRenderedPose` / `InterpolatePose` **still present** and
  still reconstructable via flag for Slice 12 deletion gate.
- Independently mergeable; stay ≤~500 LOC.

### Estimated PR size

~350–500 LOC (stay under 500; split follow-up only if display.cc churn
is high).

------------------------------------------------------------------------

## Slice 12 — Delete legacy animation paths (cutover I)

### Goal

Stop dual-feed publication into legacy, then remove replaced
pose/transient interpolation and dead dual-path code inside
`MhaiBotFaceV2` so a **single** animator→renderer path remains —
**only after** Slices 8, 10, and 11, and only when inventory T1–T7/T9
are proven migrated and behaviorally equivalent.

### Scope

- **First** (and only here): stop publishing identical live inputs into
  legacy `SetEmotion` / legacy transient state — this is the dual-feed
  teardown. Until this step, Slice 11 rollback must remain possible.
- Delete `ResolveRenderedPose` / `InterpolatePose` / duplicate pose
  builders **only if** the live rollback switch has been explicitly
  retired (post-cutover validation signed off).
- If a rollback switch is still required by product policy, defer this
  slice or keep a thin legacy stub behind `#if` until policy lifts —
  do not claim “deleted” while rollback still needs the old path.
- Keep face shell: object create/destroy, timer, Show/Hide,
  `sleep_label_`, color, transient *entry* wrappers that forward to
  coordinator (**T10**).
- Keep display alert/reaction emoji and board backlight/panel policy
  (**B2/B3/B4**).
- **Do not** move modules to `main/display/` (**ADR-006**).

#### Deletion gate (all must pass)

1. Slice 10 parity signed off (Option A canonical + hardware checklist).
2. Slice 11 soak signed off with new path primary.
3. Slice 11 rollback drill passed while dual-feed was still alive.
4. Explicit sign-off: “live `pixel_source=legacy` rollback no longer
   required; dual-feed may stop.”
5. Inventory T1–T7/T9 still behaviorally equivalent on the new path.

### Files expected to change

- `.../mhaibot_face_v2.h` / `.cc` (largest deletion/simplification)
- Possibly `.../mhaibot_display.*` (remove dual-feed bridges / flags)
- `.../eye/*` only if renames for clarity after deletion

### Public API affected

- Prefer stable board-facing methods (`StartPetting`,
  `StartGroggyWake`, `StartStartled`, Show/Hide). Legacy
  `MhaiBotFaceV2::Emotion` may be removed if unused.

### Risks

- Accidental deletion of still-needed sleep label or timer glue.
- Behavior regression after delete — mitigated by Slices 8–11; re-run
  abbreviated hardware smoke after merge.
- Deleting while rollback flag / dual-feed still required — hard-blocked.
- Stopping dual-feed before this slice — forbidden by Slice 11.

### Rollback strategy

- After deletion, rollback is **git revert** of this slice (and possibly
  Slice 11 default). Do not delete until post-cutover hardware
  validation has succeeded and the live rollback switch is no longer
  required.
- Pre-merge checklist must record: “rollback switch retired with
  sign-off” or “stubs retained”.

### Test strategy

- Re-run Slice 10 abbreviated smoke on hardware (including pet/startle/
  groggy and sleep label).
- Freenove build; grep confirms `ResolveRenderedPose` /
  `InterpolatePose` gone (or only behind agreed stubs).
- Confirm inventory migration gate still satisfied.
- Confirm dual-feed publish sites removed only after deletion-gate
  sign-off.

### Completion criteria

- One animation path: intent → coordinator/controllers → animator →
  shared blink/idle post-compose → `EyeFrame` → `LVGLEyeRenderer`.
- Legacy duplicated animation logic gone (or explicitly stub-gated).
- Dual-feed / legacy publication removed **in this slice only**.
- Still board-local; ADR-006 exit criteria **not** claimed.
- No deletion occurred before proven migration of pet, startle, groggy,
  and all other inventory Migrate items.

### Estimated PR size

~200–400 LOC deleted/changed (net often negative); keep diff reviewable
by avoiding drive-by renames.

------------------------------------------------------------------------

## Cross-cutting rules for every slice PR

1. Touch only Freenove board-local files (+ optional host tests + the
   single `eye/*.cc` GLOB if needed). No shared `main/display/eye/`.
2. Preserve behavior unless the slice explicitly adds blink/idle or
   completes flagged cutover (Slice 11).
3. No per-frame heap allocation; no LVGL from non-display contexts.
4. Animator/controllers remain LVGL-free; renderer remains policy-free.
5. Format only touched C/C++ with repo `.clang-format`.
6. Build Freenove variant before review (`scripts/build.py` /
   board name `freenove-esp32s3-display-2.8-lcd`).
7. Report what was tested vs what still needs hardware.
8. Every slice must **compile independently** atop prior merges and
   remain **independently mergeable**.
9. No slice deletes or bypasses legacy pose/transient resolution before
   Slice 12’s gates; **no slice stops dual-feeding live inputs into
   legacy before Slice 12**. Slice 11 may make the new path primary for
   pixels only.
10. Slice 9 parity compares **Option A** canonical frames (pre shared
    blink/idle); do not treat blink/idle diffs vs bare
    `ResolveRenderedPose` as mismatches.
11. Cutover/migration slices (2, 3, 7, 8, 9, 11, 12) include a
    **Rollback strategy** section in the PR description matching this
    doc.
12. No behavior deleted before equivalent proven (inventory + Slice 10).

## Out of scope (all slices)

- Transition Engine / Animation Scheduler / Render Queue
- Flat `EyeIntent` enum or single-eye `EyeFrame`
- Storing blink openness on `EyeFrame`
- Parallel application state machine replacing `DeviceState`
- Camera gaze, servo sync, network-scripted animations (`07` §13)
- Shared-module extraction before ADR-006 exit criteria
- Moving groggy backlight, reaction emoji, or panel power into the
  animator
