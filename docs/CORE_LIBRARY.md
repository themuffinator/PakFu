# PakFu Core Library

PakFu builds and installs a non-UI static library target named `pakfu_core`.

This is the public source-level seam for archive, format, search, game-profile,
and extension-contract code that does not depend on the desktop UI layer.

## Scope

`pakfu_core` includes:
- archive loading, extraction, and selective rebuild helpers
- PAK/WAD/ZIP/resources backends
- archive search indexing
- image/model/cinematic/BSP parsing helpers
- idTech metadata and idTech4 map/proc inspection helpers
- game profile and auto-detect helpers
- manifest-driven extension command loading/execution, capability negotiation, and import-manifest validation

`pakfu_core` does not include:
- Qt Widgets or viewer windows
- `PakTab`, `MainWindow`, or preview widgets
- updater UI flow
- platform shell integration

## Public Headers

The umbrella header is:

```cpp
#include <pakfu_core.h>
```

In the source tree it lives at `src/pakfu_core.h`. Installed builds place it at
`${prefix}/include/pakfu/pakfu_core.h`, alongside the public `archive/`,
`formats/`, `game/`, `extensions/`, `pak/`, `wad/`, `zip/`, and `resources/`
headers needed by that umbrella.

The umbrella header also exposes lightweight API metadata:

```cpp
PakFu::Core::api_version_string();      // "0.1.0"
PakFu::Core::api_stability_label();     // "provisional-source"
PakFu::Core::public_capabilities();     // e.g. "archive.load"
PakFu::Core::has_public_capability("format.image.decode");
```

## Build And Test

Build just the core layer:

```sh
meson compile -C builddir pakfu_core
```

Build and run the core API smoke test:

```sh
meson compile -C builddir core_api_smoke_test
meson test -C builddir core-api-smoke
```

Install the library, public headers, and `pakfu_core.pc` metadata into the
configured Meson prefix:

```sh
meson install -C builddir
```

## Consumer Example

When PakFu is installed into a pkg-config-aware prefix:

```sh
c++ -std=c++20 sample.cpp -o sample $(pkg-config --cflags --libs pakfu_core)
```

```cpp
#include <QCoreApplication>
#include <QTextStream>

#include <pakfu_core.h>

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	Q_UNUSED(app);

	QTextStream(stdout) << "PakFu core API "
	                    << PakFu::Core::api_version_string()
	                    << " ("
	                    << PakFu::Core::api_stability_label()
	                    << ")\n";

	Archive archive;
	QString error;
	if (argc > 1 && !archive.load(QString::fromLocal8Bit(argv[1]), &error)) {
		QTextStream(stderr) << error << '\n';
		return 1;
	}

	QTextStream(stdout) << archive.entries().size() << " entries\n";
	return 0;
}
```

Without pkg-config, add `${prefix}/include/pakfu` to the include path, link
against `pakfu_core`, and link the same Qt6 Core and Gui libraries used by the
PakFu build.

## API Status

The `0.x` core API is a documented source-level contract, not a binary ABI
promise. Rebuild consumers when updating PakFu, and check
`PakFu::Core::kApiMajor`, `kApiMinor`, `kApiPatch`, or
`PakFu::Core::public_capabilities()` when tooling needs to gate optional
features.
