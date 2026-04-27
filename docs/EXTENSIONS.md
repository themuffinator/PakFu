# PakFu Extensions

PakFu extensions are manifest-driven external commands. PakFu discovers JSON
manifests, negotiates command capabilities, launches the declared process, and
passes a structured JSON payload on `stdin`.

The extension layer currently supports:
- manifest discovery
- capability negotiation
- read-only archive context
- selected-entry materialization to temporary files/directories
- extension-generated imports/write-back through a host-validated manifest
- GUI launch from the `Extensions` menu
- CLI launch with `--list-plugins` and `--run-plugin`

There is no binary plugin ABI yet. Extensions are ordinary external processes.

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

## Manifest Schema

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
      "capabilities": ["archive.read", "entries.read"],
      "requires_entries": true,
      "allow_multiple": false,
      "extensions": ["cfg"]
    },
    {
      "id": "generate-cfg",
      "name": "Generate CFG",
      "description": "Generate a CFG file and import it into the archive.",
      "command": ["python", "generate_cfg.py"],
      "working_directory": ".",
      "capabilities": ["archive.read", "entries.import"]
    }
  ]
}
```

## Manifest Fields

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
- `capabilities` : optional list of requested capabilities
- `requires_entries` : optional boolean, default `false`; requires `entries.read`
- `allow_multiple` : optional boolean, default `true`; requires `entries.read` when selection constraints are meaningful
- `extensions` : optional list of allowed selected-file extensions without dots; requires `entries.read`

If `capabilities` is omitted, PakFu treats the command as a legacy read command
with:

```json
["archive.read", "entries.read"]
```

Known capabilities:
- `archive.read` : receive archive metadata in the JSON payload
- `entries.read` : receive selected archive entries materialized to temporary local paths
- `entries.import` : receive a host-owned import directory and manifest path for generated archive entries

Commands with unknown capabilities are skipped with a manifest warning.

## Execution Contract

PakFu launches the external process with:
- JSON payload on `stdin`
- separate `stdout` and `stderr` capture
- environment variables:
  - `PAKFU_EXTENSION_SCHEMA`
  - `PAKFU_EXTENSION_HOST_CAPABILITIES`
  - `PAKFU_EXTENSION_COMMAND_CAPABILITIES`
  - `PAKFU_EXTENSION_PLUGIN_ID`
  - `PAKFU_EXTENSION_COMMAND_ID`
  - `PAKFU_EXTENSION_IMPORT_ROOT` for commands with `entries.import`
  - `PAKFU_EXTENSION_IMPORTS_PATH` for commands with `entries.import`

Relative executable paths are resolved from the command's working directory.

## JSON Payload

The payload schema id is:

```text
pakfu-extension/v1
```

Example payload:

```json
{
  "schema": "pakfu-extension/v1",
  "host": {
    "schema": "pakfu-extension/v1",
    "capabilities": ["archive.read", "entries.read", "entries.import"],
    "import_root": "/absolute/path/to/temp/extension-import-1",
    "import_manifest_path": "/absolute/path/to/temp/extension-import-1/pakfu-imports.json"
  },
  "command": {
    "plugin_id": "example",
    "plugin_name": "Example Tools",
    "command_id": "inspect-cfg",
    "command_name": "Inspect CFG",
    "description": "Run an external checker on one selected CFG file.",
    "manifest_path": "/absolute/path/to/plugin.json",
    "capabilities": ["archive.read", "entries.read"]
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

The `entries` array is empty when the command does not request `entries.read`.
The `import_root` and `import_manifest_path` host fields are present only for
commands that request `entries.import`.

## Import/Write-Back Contract

Commands with `entries.import` can create files below `import_root` and then
write an import manifest to `import_manifest_path`.

Import manifest schema:

```json
{
  "schema": "pakfu-extension-imports/v1",
  "imports": [
    {
      "archive_name": "scripts/generated.cfg",
      "local_path": "generated/generated.cfg",
      "mode": "add_or_replace",
      "mtime_utc_secs": 1714156800
    }
  ]
}
```

Import fields:
- `archive_name` : target archive path; must be a safe relative archive entry path
- `local_path` : file path under `import_root`; relative paths are resolved from `import_root`
- `mode` : currently only `add_or_replace`
- `mtime_utc_secs` : optional UTC timestamp for the pending archive entry

PakFu validates import manifests before applying them:
- the manifest must use `pakfu-extension-imports/v1`
- every import source must be a file under `import_root`
- target archive paths must be safe relative entry names
- unsupported modes are rejected

GUI behavior:
- import outputs are added as pending archive changes
- imported entries can be undone with the normal undo stack
- the archive is not saved until the user chooses Save or Save As
- import-capable commands are disabled for read-only views, mounted archive views, and Pure PAK-protected official archives

CLI behavior:
- PakFu validates and reports requested imports
- CLI write-back is not applied automatically
- import files are retained in the reported temporary directory when imports are requested
- scripts can consume the reported import paths or run the command through the GUI workflow when interactive write-back is desired

## GUI Behavior

Open an archive tab, select entries if the command requires them, then use:

```text
Extensions -> <command>
```

PakFu validates the current selection and editable state before launch. If a
command is disabled in the menu, its tooltip explains why.

## CLI Behavior

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

For commands with `entries.read`, `--entry` and `--prefix` pass matching files
as materialized temp files to the extension command. Commands without
`entries.read` receive an empty `entries` array.
