# UX Validation Artifacts

This document turns the UX roadmap in
`docs/proposals/ux-workflow-analysis-1-may-2026.md` into concrete acceptance
artifacts. It records the completed implementation evidence and the runtime
proof that release QA should refresh for each tagged build: manual keyboard QA,
pointer/target checks, contrast review, layout/scale review, status and modality
review, i18n review, benchmark evidence, consistency review, telemetry
inventory, and staged validation.

Use this with `scripts/ux_roadmap_audit.py`. The script provides static
completion evidence; this document provides the reusable runtime/release
validation matrices.

## Release Gate Summary

| Gate | Required artifact | Owner | Release threshold |
|---|---|---|---|
| Keyboard QA | Completed keyboard matrix below on Windows, macOS, and Linux | QA | No blocker or high severity keyboard traps |
| Pointer/target QA | Target-size capture sheet and exception log | UX + QA | Primary actions meet target rules or have documented alternatives |
| Contrast matrix | Theme token contrast table for every bundled theme | Design + QA | Text and state contrast pass agreed thresholds |
| Layout/scale audit | Screenshot set at 100%, 150%, and 200% scale | QA | No clipped primary controls or unreadable text |
| Status/modality | Modal inventory with replacement decision | Product + UX | Noncritical modals have an accepted keep/replace decision |
| i18n | Pseudo-localized and long-string run notes | Localization + QA | App shell does not clip core labels or compose unsafe strings |
| Performance | Benchmark run with corpus summary | Performance + QA | No regression above accepted budget |
| Consistency | Terminology and icon audit | UX + Docs | Core verbs and icons are consistent across surfaces |
| Telemetry | Event inventory and privacy review | Product + Engineering | Events are documented before instrumentation |
| A/B validation | Experiment plan and success metrics | Product + UX Research | High-friction roadmap changes have measurable success criteria |
| Release hardening | Checklist in `docs/ROADMAP_COMPLETION.md` | Release | All required rows complete or explicitly deferred |

## Keyboard QA Matrix

Run the matrix with a clean profile and with at least one real game installation
or representative fixture folder. Record platform, Qt version, screen scale, and
input device.

| Workflow | Keys to verify | Acceptance |
|---|---|---|
| Launch and select installation | Tab, Shift+Tab, arrows, Enter, Escape | Focus starts in a useful location, visible focus never disappears, selection can be opened, Escape cancels only when safe |
| Quick open/search | Ctrl+K, typing, arrows, Enter, Escape | Search is reachable from the app shell, results can be chosen without a mouse, focus returns to the invoking surface |
| Open archive or folder | Alt/menu path, Tab, Enter | Native dialog opens from keyboard path and returns focus to the new archive tab |
| Archive details browsing | Arrows, Home/End, Page Up/Down, Enter, Space, context menu key | Current item, selection, activation, and context menu are visible and screen-reader discoverable |
| Archive icon/gallery browsing | Arrows, Home/End, Page Up/Down, Enter, Space | Keyboard movement matches visual order and does not skip items |
| Search within archive | Ctrl+F or visible search focus path, typing, Enter | Results update without trapping focus; clearing search restores browse context |
| Extract selected/all | Keyboard menu path, Tab, Enter, Escape | Destination selection and confirmation are keyboard-complete; destructive decisions default to the safest button |
| Cut/copy/paste/rename/delete | Standard shortcuts, F2, Delete | Shortcut behavior matches platform expectations and confirmations return focus correctly |
| Preview pane controls | Tab, arrows, Space, Enter | Media, image, text, and 3D controls have visible focus and accessible names |
| Detached viewer windows | Tab, arrows, Space, F11/Escape where supported | Previous/next, playback, fullscreen, close, and dock/undock actions are keyboard-complete |
| Workspace lenses | Tab, arrows, Enter | Lens navigation is reachable and current lens is visible and announced |
| Preferences | Tab, arrows, Space, Enter, Escape | Changes are operable without a mouse and labels remain associated with controls |
| Update flow | Tab, Enter, Escape | User-initiated update checks do not trap focus; automatic update notices are nonblocking unless a decision is required |

## Pointer And Target QA

Measure pointer targets from screenshots or accessibility tooling at 100%, 150%,
and 200% scale. Record any exception with rationale.

| Surface | Controls to measure | Acceptance |
|---|---|---|
| App toolbar and archive toolbar | New/open/extract/add/search/view/preview actions | Primary actions are at least 24x24 CSS px equivalent with adequate spacing; preferred target is 32x32 or larger |
| Viewer chrome | Previous/next, playback, fullscreen, dock/undock, close | Icon-only controls have tooltips and accessible names |
| Tables and lists | Rows, expanders, drag handles, context areas | Selection and drag affordances are easy to hit and have clear hover/drop states |
| Breadcrumbs | Each breadcrumb segment | Segments are individually clickable or the whole control exposes a reliable alternative |
| Preferences | Checkboxes, combos, sliders, browse buttons | Labels are clickable where Qt supports it and controls do not collapse under scaling |
| Dialog buttons | Accept/cancel/destructive choices | Safe default is clear; destructive action is separated visually and by tab order where practical |

## Contrast Matrix

Use actual rendered colors, not design intent. Test every bundled theme plus the
platform default palette if it is available.

| Token or state | Minimum acceptance | Notes |
|---|---|---|
| Body text on surface | WCAG AA normal text contrast | Include table cells, labels, menus, and dialogs |
| Secondary/helper text | WCAG AA normal text contrast unless purely decorative | `palette(mid)` and hard-coded muted colors need special attention |
| Selection text/background | WCAG AA for selected text | Check active and inactive windows |
| Focus ring | WCAG non-text contrast target against adjacent colors | Must remain visible in light, dark, and colorful themes |
| Icons on toolbar buttons | WCAG non-text contrast target | Include disabled, hover, pressed, and checked states |
| Borders/dividers | Visible enough to communicate grouping when meaningful | Decorative borders may be exempt if not conveying state |
| Error/warning/success states | Text contrast plus non-color cue | Do not rely on hue alone |
| Tooltips and status messages | Body text threshold | Include native and custom status surfaces |

## Layout And Scale Audit

Capture the following at 1366x768, 1920x1080, and one narrow window width near
800 px. Repeat at 100%, 150%, and 200% scale with the default font and with one
larger system font.

| Screen | Required states | Acceptance |
|---|---|---|
| Workspace | Overview, Search, Changes, Validation, Capabilities | No overlapping toolbar actions; tables remain scrollable; lens labels do not clip |
| Archive tab | Details view, icon view, search active, preview detached | Browse and preview panes keep useful minimum sizes |
| Preview pane | Image, audio, video/cinematic, model/BSP, text/config | Controls remain reachable and labels do not overlap media |
| Installations dialog | Empty, populated, auto-detect result, edit dialog | Long paths and translated labels wrap or elide intentionally |
| Preferences | Each section, long labels, disabled controls | Controls align and do not require horizontal scrolling except for data tables |
| File associations | Windows-capable and non-Windows states | Disabled or unsupported states explain themselves without crowding |
| Update notices | Latest version, update available, failure | Nonblocking notices do not cover primary workflow unexpectedly |

## Status And Modality Checks

Every modal should have a reason. Use the static modal inventory from
`scripts/ux_roadmap_audit.py` as the starting list.

| Modal type | Keep modal when | Prefer inline/status when |
|---|---|---|
| Destructive confirmation | Data loss or irreversible action is possible | The action is undoable and already reflected in history |
| File overwrite conflict | User must choose a conflict policy | A remembered policy or per-row conflict table exists |
| Validation error | User cannot proceed without fixing input | Error can be attached to the specific field |
| Informational success | Rarely; only for critical confirmation | Completion can be shown in status bar, message bar, or toast |
| Update available | User initiated the check or install handoff is required | Automatic check found noncritical information |
| Long-running progress | Blocking is unavoidable and cancellation matters | Background work can continue while the user browses |

Acceptance requires each modal in the inventory to be labeled as keep, replace,
merge, or defer, with a reason and a target milestone.

### Classified Modal Inventory

The current roadmap sweep removes noncritical `QMessageBox::information`
call sites. Remaining modal surfaces are classified below.

| Surface | Modal purpose | Decision | Reason |
|---|---|---|---|
| Native file dialogs | Choose open/save/extract/add paths | Keep | Platform file picking is a focused user decision and preserves native bookmarks/history |
| Installations chooser/editor | Select or configure installation context | Keep | Startup/profile selection is a bounded setup task with explicit accept/cancel |
| Archive open choice | Choose Quick Inspect, install copy, or move | Keep | User must select a persistence policy when defaults are not enough |
| File overwrite/name conflicts | Choose overwrite, keep both, skip, or cancel | Keep | Data loss or duplicate-name behavior requires an explicit policy |
| Destructive delete/discard/unsaved prompts | Confirm data loss or discard | Keep | Destructive choices must be intentional and safely defaulted |
| Validation/error warnings | Explain why an operation cannot proceed | Keep | User action is required; field-level inline status is used where practical |
| Long-running progress | Show cancellable extract/convert/add/download work | Keep | Blocking/cancellable operations need progress and cancellation affordances |
| Update available/install handoff | Choose download/install, release page, skip, or later | Keep | User must decide whether to download, install, skip, or defer |
| Update/latest/downloaded noncritical status | Report FYI completion or already-latest state | Replace | Routed through status bar/transient status instead of an information dialog |
| Auto-detect and file-association success/no-result notices | Report noncritical results | Replace | Routed through inline dialog status labels |

## Internationalization Checks

Static signals are necessary but not enough. Run at least one pseudo-localized
build or local override that expands strings by 30-50%.

| Check | Acceptance |
|---|---|
| Translator bootstrap | `QTranslator` is installed before widgets are created |
| User-visible strings | App shell strings use `tr()` or `QCoreApplication::translate()` |
| Concatenation | No user-visible sentence depends on English word order through string concatenation |
| Plurals | Counts use plural-aware APIs or have a tracked remediation |
| Dates/numbers | Locale-sensitive values use Qt locale formatting |
| Long paths | Paths elide or wrap intentionally without hiding adjacent controls |
| Pseudo-localization | Core workflows remain operable with expanded strings |

## Performance And Benchmark Checks

Benchmarks need a repeatable corpus. Keep private game assets out of the repo;
store only corpus descriptions and generated fixture metadata.

| Scenario | Metric | Acceptance evidence |
|---|---|---|
| Startup to main window | Wall time and update-check behavior | Main window is not blocked by network latency |
| Open small archive | Time to listing and first preview | Baseline does not regress release-to-release |
| Open large archive | Time to listing, sort/filter latency, memory high-water mark | UI remains responsive during load and browsing |
| Search large archive | Time to first result and full result set | Search does not visibly stall the UI thread |
| Preview image/audio/video/model/BSP | First preview time and repeat preview time | Cache behavior improves repeat preview or is explained |
| Extract selected/all | Throughput and cancellation/reporting behavior | Progress is visible and failures are summarized |
| Workspace refresh | Refresh duration with many open archives | Tables update without freezing input |

## Consistency Checks

Use this as a cross-surface review sheet before release.

| Area | Acceptance |
|---|---|
| Verbs | Same action uses the same verb: Open, Inspect, Extract, Convert, Add, Remove, Delete, Save |
| Icons | Same icon means the same action across workspace, archive tabs, viewers, and dialogs |
| Destructive actions | Destructive labels and button roles are consistent and safe by default |
| Search | Search fields have consistent placeholder language, keyboard path, and clear behavior |
| Preview | Preview fallback, detached state, and unavailable-state language match across asset types |
| Status | Success, progress, warning, and error messages use consistent severity and placement |
| Help text | Tooltips and accessible descriptions clarify icon-only controls without duplicating visible labels |
| CLI/docs | GUI behavior changes that affect key actions are reflected in README/help text when relevant |

## Telemetry Event Inventory

PakFu should remain privacy-conscious. Instrument only operational events needed
to validate roadmap decisions, and make collection opt-in or local-only unless a
future privacy policy explicitly says otherwise.

| Event | Properties | Purpose |
|---|---|---|
| `app_start_completed` | platform, version, elapsed_ms, restored_installation | Measures startup friction |
| `installation_selected` | source, elapsed_ms, profile_kind | Measures onboarding and profile switching |
| `archive_open_started` / `archive_open_completed` | source, archive_kind, file_count_bucket, elapsed_ms, result | Measures open flow and Quick Inspect impact |
| `preview_requested` / `preview_completed` | asset_kind, renderer, elapsed_ms, cached, result | Measures preview reliability and performance |
| `extract_started` / `extract_completed` | selection_count_bucket, total_bytes_bucket, elapsed_ms, result | Measures core archive workflow clarity |
| `search_started` / `search_completed` | scope, query_length_bucket, result_count_bucket, elapsed_ms | Measures search discoverability and scale |
| `modal_shown` | modal_kind, user_initiated, result | Tracks interruption burden |
| `inline_status_shown` | severity, surface, user_action_required | Compares inline status to modal behavior |
| `keyboard_shortcut_used` | command_id, surface | Measures command discoverability without logging content |
| `error_report_created` | crash_or_error_kind, platform, version | Tracks release hardening without collecting private paths |

Never log full archive paths, file names, search queries, usernames, installation
directories, or asset content.

## A/B Validation Plan

Use staged rollout or local research sessions when production telemetry is not
available.

| Test | Variant A | Variant B | Success metric | Guardrail |
|---|---|---|---|---|
| Archive open | Current prompt-first flow | Quick Inspect default with secondary install action | Time to first preview, cancellation rate | No increase in accidental install-copy/move actions |
| Workspace navigation | Current multi-section layout | Sidebar lens layout | Time to reach Search/Changes/Validation | No loss in keyboard completion |
| Status communication | Modal completion/error notices | Inline message bar/toast for noncritical states | Lower modal count per session | Error comprehension stays equal or better |
| Search placement | Workspace-local search | Global command/search affordance | Search use rate and time to result | No conflict with platform shortcuts |
| Preview inspector | Current mixed preview controls | Stable right-side inspector hierarchy | Browse-to-preview task time | No decrease in preview feature discovery |

For research sessions, use five to eight participants per high-friction surface,
with tasks drawn from real modding flows: open an unknown archive, locate a
texture, preview it, extract it, find dependencies, and compare a changed asset.
