# Eye Animation Architecture

This directory contains the design documents for the MhaiBot Eye
Animation Framework.

## Purpose

Define a consistent architecture before implementation so work can be
split into small, reviewable pull requests. The canonical design is the
model in `05`, `06`, and `07`. All other documents in this directory
must describe that same architecture.

Implementation is planned as an **extraction** from the existing
board-local Freenove face (`MhaiBotFaceV2`), not a greenfield rewrite
under shared display modules.

## Canonical Sources

These three documents are the source of truth for types, layering,
behavior, and integration order:

| Doc | Role |
|---|---|
| `05-state-machine.md` | `EyeActivity` / `EyeEmotion` / `EyeTransient`, transitions, priority, threading ownership |
| `06-rendering-pipeline.md` | `EyeIntent` / `EyeFrame` / `EyeGeometry`, pipeline stages, LVGL rules, integration sequence |
| `07-animation-spec.md` | Visible behavior, timing ranges, composition order, hardware validation |

If another document disagrees with these three, treat `05`/`06`/`07` as
correct and update the outlier.

## Reading Order

1. `00-overview.md` — scope, principles, and binding to existing code
2. `ADR.md` — architecture decisions
3. `01-eye-animation-framework.md` — layered architecture aligned with `05`/`06`
4. `02-eye-animation-api.md` — public interfaces, ownership, mailbox contract
5. `03-eye-animation-sequence.md` — runtime update sequence and timing model
6. `04-eye-animation-roadmap.md` — single implementation roadmap (same as `08`)
7. `05-state-machine.md` — canonical state model
8. `06-rendering-pipeline.md` — canonical pipeline and data model
9. `07-animation-spec.md` — canonical behavior specification
10. `08-implementation-plan.md` — phased plan matching `06` §11 / `04`
11. `09-implementation-slices.md` — reviewable PR slices (≤~500 LOC): dual-path mailbox cutover, legacy transient migration inventory, parity gate, then delete-legacy

Filenames on disk may use a double extension (`.md.md`). Paths above omit
the redundant suffix for readability; use the on-disk names when linking
in this repository.

## Design Principles

- Separate interaction intent from animation geometry and from LVGL drawing.
- Canonical intent is a structured `EyeIntent` (activity + emotion + gaze + blink policy), not a flat enum.
- Canonical frame is stereo `EyeFrame` (`left` / `right` `EyeGeometry` + opacity).
- One `EyeAnimator` composes controller contributions; one thin `LVGLEyeRenderer` mutates pre-created LVGL objects.
- No Transition Engine, Animation Scheduler, or Render Queue.
- `DeviceState` remains the application source of truth; `EyeActivity` is derived through an adapter.
- Keep the first implementation board-local under the Freenove board; do not move into shared `main/display/` until **ADR-006** exit criteria are met (second consumer + stable types + no board-only policy in shared code + ADR amendment).
- Time-based `Update(delta_ms)`; do not assume a fixed 60 FPS frame count.
- Resolved decisions: mapping **ADR-002**, ownership **ADR-003**, blink openness **ADR-004**, mailbox **ADR-005**, shared-exit **ADR-006** (see `ADR.md` / `02`).

## Status

Planning / Architecture phase. Implementation should follow the single
roadmap in `04` / `08` (order from `06` §11).
