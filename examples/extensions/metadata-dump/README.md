# Metadata Dump Example

This example demonstrates the safest baseline extension shape:

- a manifest with explicit `archive.read` and `entries.read` capabilities
- no archive write-back
- a tiny C++ command that reads the PakFu JSON payload from `stdin`
- stdout/stderr output captured by the GUI and CLI

Build:

```sh
c++ -std=c++20 metadata_dump.cpp -o metadata-dump
```

Run from the repository root:

```sh
pakfu --cli --plugin-report --plugin-dir examples/extensions/metadata-dump
pakfu --cli --run-plugin pakfu.example.metadata:dump-selection --plugin-dir examples/extensions/metadata-dump --entry scripts/autoexec.cfg path/to/archive.pk3
```

