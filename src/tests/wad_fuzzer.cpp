#include <cstddef>
#include <cstdint>

#include <QStringList>

#include "tests/fuzz_harness_utils.h"
#include "wad/wad_archive.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	ensure_qt_fuzz_app();

	const QByteArray bytes = fuzz_input_bytes(data, size);
	for (const QString& suffix : QStringList{"wad", "wad2", "wad3"}) {
		auto file = write_fuzz_input_file(bytes, suffix);
		if (!file) {
			continue;
		}

		WadArchive archive;
		QString error;
		if (!archive.load(file->fileName(), &error)) {
			continue;
		}

		exercise_archive_contents(&archive);
	}

	return 0;
}
