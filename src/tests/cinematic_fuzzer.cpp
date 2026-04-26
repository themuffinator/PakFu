#include <cstddef>
#include <cstdint>

#include <QStringList>

#include "formats/cinematic.h"
#include "tests/fuzz_harness_utils.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	ensure_qt_fuzz_app();

	const QByteArray bytes = fuzz_input_bytes(data, size);
	for (const QString& suffix : QStringList{"cin", "roq"}) {
		auto file = write_fuzz_input_file(bytes, suffix);
		if (!file) {
			continue;
		}

		QString error;
		auto decoder = open_cinematic_file(file->fileName(), &error);
		if (!decoder) {
			continue;
		}

		CinematicFrame frame;
		decoder->decode_next(&frame, nullptr);
		decoder->reset(nullptr);
		decoder->decode_next(&frame, nullptr);

		const int count = decoder->frame_count();
		if (count > 0 && count < 4) {
			decoder->decode_frame(0, &frame, nullptr);
		}
	}

	return 0;
}
