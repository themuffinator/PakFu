# PakFu Extensions

PakFu's first extension phase is a manifest-driven external command layer.
Extensions are discovered from JSON manifests, and PakFu launches the declared
command with a structured JSON payload on `stdin`.

This phase is intentionally small:
- manifest discovery
- read-only archive context
- selected-entry materialization to temporary files/directories
- GUI launch from the `Extensions` menu
- CLI launch with `--list-plugins` and `--run-plugin`

There is no binary plugin ABI yet, and there is no write-back/import contract
from extensions into archives in this phase.

## Discovery

PakFu searches these directories for manifests:
- `<pakfu-dir>/plugins`
- `./plugins` from the current working directory
- the platform app-data `plugins` directory for PakFu

CLI callers can add more search roots with:

```text
--plugin-dir <dir>
```

PakFu looks for either:
- `plugin.json`
- `*.pakfu-plugin.json`

## Manifest schema

Schema version: `1`

Minimal example:

```json
{
  "schema_version": 1,
  "id": "example",
  "name": "Example Tools",
  "description": "Sample extension commands.",
  "commands": [
    {
      "id": "inspect-cfg",
      "name": "Inspect CFG",
      "description": "Run an external checker on one selected CFG file.",
      "command": ["python", "inspect_cfg.py"],
      "working_directory": ".",
      "requires_entries": true,
      "allow_multiple": false,
      "extensions": ["cfg"]
    }
  ]
}
```

## Manifest fields

Top-level fields:
- `schema_version` : currently `1`
- `id` : plugin identifier (`A-Z`, `a-z`, `0-9`, `_`, `.`, `-`)
- `name` : display label
- `description` : optional description
- `commands` : array of command objects

Command fields:
- `id` : command identifier
- `name` : display label
- `description` : optional description
- `command` : argv array; first item is the executable
- `working_directory` : optional; relative paths resolve from the manifest directory
- `requires_entries` : optional boolean, default `false`
- `allow_multiple` : optional boolean, default `true`
- `extensions` : optional list of allowed selected-file extensions without dots

## Execution contract

PakFu launches the external process with:
- JSON payload on `stdin`
- separate `stdout` and `stderr` capture
- environment variables:
  - `PAKFU_EXTENSION_SCHEMA`
  - `PAKFU_EXTENSION_PLUGIN_ID`
  - `PAKFU_EXTENSION_COMMAND_ID`

Relative executable paths are resolved from the command's working directory.

## JSON payload

The payload schema id is:

```text
pakfu-extension/v1
```

Example payload:

```json
{
  "schema": "pakfu-extension/v1",
  "command": {
    "plugin_id": "example",
    "plugin_name": "Example Tools",
    "command_id": "inspect-cfg",
    "command_name": "Inspect CFG",
    "description": "Run an external checker on one selected CFG file.",
    "manifest_path": "/absolute/path/to/plugin.json"
  },
  "archive": {
    "path": "/absolute/path/to/archive.pk3",
    "readable_path": "/absolute/path/to/archive.pk3",
    "format": "zip",
    "mounted_entry": "",
    "current_prefix": "scripts/",
    "quakelive_encrypted_pk3": false,
    "wad3": false,
    "doom_wad": false
  },
  "entries": [
    {
      "archive_name": "scripts/autoexec.cfg",
      "local_path": "/absolute/path/to/temp/scripts/autoexec.cfg",
      "is_dir": false,
      "size": 128,
      "mtime_utc_secs": 1714156800
    }
  ]
}
```

## GUI behavior

Open an archive tab, select entries if the command requires them, then use:

```text
Extensions -> <command>
```

PakFu validates the current selection before launch. If a command is disabled in
the menu, its tooltip explains why.

## CLI behavior

List discovered commands:

```text
pakfu --cli --list-plugins
```

Run a command against an archive:

```text
pakfu --cli --run-plugin example:inspect-cfg path/to/archive.pk3
```

Pass selected files with existing archive selectors:

```text
pakfu --cli --run-plugin example:inspect-cfg --entry scripts/autoexec.cfg path/to/archive.pk3
pakfu --cli --run-plugin example:inspect-cfg --prefix scripts path/to/archive.pk3
```

In the CLI path for this phase, `--entry` and `--prefix` pass matching files as
materialized temp files to the extension command.
