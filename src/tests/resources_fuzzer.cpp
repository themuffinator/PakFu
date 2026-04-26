#include <cstddef>
#include <cstdint>

#include "resources/resources_archive.h"
#include "tests/fuzz_harness_utils.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	ensure_qt_fuzz_app();

	const QByteArray bytes = fuzz_input_bytes(data, size);
	auto file = write_fuzz_input_file(bytes, "resources");
	if (!file) {
		return 0;
	}

	ResourcesArchive archive;
	QString error;
	if (!archive.load(file->fileName(), &error)) {
		return 0;
	}

	exercise_archive_contents(&archive);
	return 0;
}
