#include <cstddef>
#include <cstdint>

#include <QStringList>

#include "formats/model.h"
#include "tests/fuzz_harness_utils.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
	ensure_qt_fuzz_app();

	const QByteArray bytes = fuzz_input_bytes(data, size);
	for (const QString& suffix : QStringList{
	       "mdl",
	       "md2",
	       "md3",
	       "mdc",
	       "md4",
	       "mdr",
	       "skb",
	       "skd",
	       "mdm",
	       "glm",
	       "iqm",
	       "md5mesh",
	       "tan",
	       "obj",
	       "lwo",
	     }) {
		auto file = write_fuzz_input_file(bytes, suffix);
		if (!file) {
			continue;
		}

		QString error;
		(void)load_model_file(file->fileName(), &error);
	}

	return 0;
}
