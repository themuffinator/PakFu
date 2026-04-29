# PakFu Extension Examples

These examples are small, source-first extension packs that can be copied into a
PakFu extension search directory while developing external tooling.

Typical local workflow:

```sh
cd examples/extensions/metadata-dump
c++ -std=c++20 metadata_dump.cpp -o metadata-dump
pakfu --cli --plugin-report --plugin-dir .
pakfu --cli --run-plugin pakfu.example.metadata:dump-selection --plugin-dir . --entry scripts/autoexec.cfg path/to/archive.pk3
```

On Windows, build `metadata-dump.exe` in the same folder. The manifest uses a
relative `./metadata-dump` command so PakFu resolves it from the example's
working directory.

