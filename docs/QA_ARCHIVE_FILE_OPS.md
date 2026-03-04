# Archive File Operations QA

This checklist covers archive file handling quality for:
- selection (single/multi/range/toggle/marquee),
- cut/copy/paste,
- drag/drop across folders/tabs/apps,
- modifier-key intent (`copy` / `move` / `link-request`),
- collision and safety behavior.

## Automated Smoke QA

Run the built-in practical smoke checks:

```sh
./builddir/src/pakfu --cli --qa-practical
```

Windows PowerShell:

```powershell
.\builddir\src\pakfu.exe --cli --qa-practical
```

Headless (CI, typically Linux) example when the `offscreen` platform plugin is available:

```sh
QT_QPA_PLATFORM=offscreen ./builddir/src/pakfu --cli --qa-practical
```

Automated coverage:
- details view: `Shift` range selection,
- details view: toggle selection (`Ctrl` on Windows/Linux, `Cmd` on macOS),
- details and icon views: select-all shortcut (`Ctrl/Cmd+A`),
- details and icon views: marquee/rubber-band selection from empty area,
- drag/drop readiness across all browser views (`Details`, `List`, `Small Icons`, `Large Icons`, `Gallery`),
- lock-state gating for modification operations (import drop disallowed when tab is locked),
- drag/drop modifier policy resolution:
  - Windows/Linux: `Ctrl=Copy`, `Shift=Move`, `Ctrl+Shift=Link request`,
  - macOS: `Option=Copy`, `Shift=Move`, `Control+Option=Link request`,
- fallback to supported action when requested action is unavailable.

## Manual Cross-Platform Matrix

Use this matrix on each release candidate:
- Windows 11 (Explorer integration),
- macOS (Finder integration),
- Linux (at least one GTK and one KDE file manager).

### Selection And Navigation

1. Single-click selects one item; double-click opens file or enters folder.
2. `Shift+Click` selects contiguous range anchored from current item.
3. `Ctrl+Click` (Windows/Linux) or `Cmd+Click` (macOS) toggles item selection.
4. `Ctrl/Cmd+A` selects all visible items in active view.
5. Dragging in empty area performs marquee selection:
   - details view row marquee,
   - icon/list view rubber-band selection.
6. Selection and current item are preserved across view switches and refreshes when items still exist.

### Copy/Cut/Paste

1. Copy selected file(s)/folder(s), paste in same folder:
   - collision prompt appears (`Overwrite`, `Keep Both`, `Skip`, `Cancel`),
   - `Keep Both` generates unique names.
2. Cut then paste in a different folder in same tab:
   - imported items appear in destination,
   - originals removed only after successful import,
   - clipboard converts to copy payload after completed move.
3. Cut/Paste in locked or read-only tabs:
   - modification actions are disabled or rejected,
   - no source deletion occurs.

### Drag/Drop (In-App)

1. Drag between folders in same tab:
   - default intent matches platform behavior,
   - modifiers override intent (`copy/move`) per platform convention.
2. Drag between tabs:
   - move deletes in source tab only when source tab is editable and move is valid,
   - copy path remains non-destructive.
3. Drag folder into itself or same path:
   - blocked cleanly (no-op or safe rejection).
4. Drop into read-only destination:
   - rejected with no mutation.

### Drag/Drop (External Apps)

1. Drag files from Explorer/Finder/file manager into PakFu:
   - imports as copy,
   - no attempt to delete source files.
2. Drag files from PakFu into Explorer/Finder/file manager:
   - exported data appears as expected.
3. Mixed payloads and unicode/space-heavy paths resolve correctly.

### Undo/Redo And Persistence

1. Copy/move/import/delete actions are undoable and redoable.
2. After save/reload, resulting archive state matches visible file browser state.
3. No stale selections or crashes after undo/redo cycles with multi-selection.
