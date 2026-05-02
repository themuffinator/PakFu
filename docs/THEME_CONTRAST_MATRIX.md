# Theme Contrast Matrix

PakFu derives UI styling from semantic design tokens in `src/ui/design_tokens.h`.
The bundled themes still define `QPalette` values first; tokens translate those
palette roles into names that are stable for QSS, future widgets, and accessibility
checks.

## Token Set

| Token | Source/derivation | Intended use |
|---|---|---|
| `surface` | `QPalette::Window` | App chrome and default containers |
| `raised_surface` | `QPalette::Button` | Buttons, tabs, headers, menus that sit above the surface |
| `border` | `QPalette::Mid` | Control and view outlines |
| `text_primary` | `QPalette::WindowText` | Main labels and control text |
| `text_secondary` | Disabled text if it reaches 4.5:1, otherwise a readable primary/surface mix | Metadata and helper labels |
| `accent` | `QPalette::Link`, contrast-adjusted to 4.5:1 on `surface` | Links and accent text/icons |
| `success`, `warning`, `error` | Surface-aware status colors, contrast-adjusted to 4.5:1 | Status text/icons |
| `selection` | `QPalette::Highlight` | Selected rows, active menu items, checked tool buttons |
| `selection_text` | `QPalette::HighlightedText`, or black/white if needed for 4.5:1 | Text on `selection` |
| `focus` | `QPalette::Highlight`, contrast-adjusted to 3:1 on `surface` | Keyboard focus rings |
| `disabled_surface` | Disabled button role, falling back to a raised/surface mix | Disabled control fill |
| `disabled_text` | Disabled text if it reaches 3:1, otherwise a readable fallback | Disabled labels and controls |
| `preview_scrim` | Black with surface-aware alpha | Overlay/scrim use in preview surfaces |

## Acceptance Thresholds

| Pair/state | Minimum |
|---|---:|
| `text_primary` on `surface` | 4.5:1 |
| `text_secondary` on `surface` | 4.5:1 |
| `disabled_text` on `surface` | 3.0:1 |
| `selection_text` on `selection` | 4.5:1 |
| `focus` against `surface` | 3.0:1 |
| `accent`, `success`, `warning`, `error` on `surface` | 4.5:1 |

`System` theme is intentionally not fixed in this table because its palette comes
from the host OS and Qt style. It is inside the completed roadmap scope through
the same token derivation and contrast-correction code path in
`src/ui/design_tokens.h`; release QA should still capture platform screenshots
for System mode because the raw palette differs by OS, Qt style, and user theme.

## Bundled Theme Matrix

Ratios below are calculated from the current bundled palette constants and token
derivation as of this pass.

| Theme | primary/surface | secondary/surface | disabled/surface | selection text/selection | focus/surface | accent/surface | success/surface | warning/surface | error/surface |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Light | 17.35 | 7.13 | 3.17 | 4.67 | 4.24 | 5.24 | 4.70 | 4.98 | 6.13 |
| Dark | 14.87 | 8.18 | 4.02 | 6.01 | 5.08 | 6.75 | 9.23 | 10.01 | 6.59 |
| Creamy Goodness | 12.78 | 5.65 | 3.40 | 5.29 | 3.21 | 4.95 | 4.79 | 5.06 | 5.27 |
| Vibe-O-Rama | 15.86 | 8.59 | 4.19 | 5.82 | 5.08 | 8.20 | 9.53 | 10.34 | 6.81 |
| Midnight | 16.14 | 8.65 | 4.29 | 5.05 | 4.51 | 8.44 | 9.76 | 10.59 | 6.97 |
| Spring Time | 16.04 | 6.51 | 3.27 | 7.28 | 3.30 | 4.66 | 4.69 | 4.98 | 6.12 |
| Dark Matter | 17.05 | 8.89 | 4.18 | 5.69 | 3.50 | 7.04 | 10.36 | 11.24 | 7.40 |
