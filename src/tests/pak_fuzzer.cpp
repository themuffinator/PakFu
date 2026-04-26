#include <cstddef>
#include <cstdint>

#include <QStringList>

#include "pak/pak_archive.h"
#include "tests/fuzz_harness_utils.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	ensure_qt_fuzz_app();

	const QByteArray bytes = fuzz_input_bytes(data, size);
	for (const QString& suffix : QStringList{"pak", "sin"}) {
		auto file = write_fuzz_input_file(bytes, suffix);
		if (!file) {
			continue;
		}

		PakArchive archive;
		QString error;
		if (!archive.load(file->fileName(), &error)) {
			continue;
		}

		exercise_archive_contents(&archive);
	}

	return 0;
}
