#include <cstddef>
#include <cstdint>

#include <QColor>
#include <QStringList>
#include <QVector>

#include "formats/image_loader.h"
#include "tests/fuzz_harness_utils.h"

namespace {
const QVector<QRgb>& fuzz_palette() {
	static const QVector<QRgb> palette = [] {
		QVector<QRgb> out(256);
		for (int i = 0; i < out.size(); ++i) {
			out[i] = QColor(i, i, i).rgba();
		}
		return out;
	}();
	return palette;
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	ensure_qt_fuzz_app();

	const QByteArray bytes = fuzz_input_bytes(data, size);
	ImageDecodeOptions options;
	options.palette = &fuzz_palette();
	for (const QString& file_name : QStringList{
	       "fixture.tga",
	       "fixture.pcx",
	       "fixture.wal",
	       "fixture.swl",
	       "fixture.m8",
	       "fixture.m32",
	       "fixture.dds",
	       "fixture.ftx",
	       "fixture.lmp",
	       "fixture.mip",
	       "fixture.png",
	       "fixture.jpg",
	     }) {
		(void)decode_image_bytes(bytes, file_name, options);
	}

	return 0;
}
