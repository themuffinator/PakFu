#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include "archive/archive_entry.h"

inline void ensure_qt_fuzz_app() {
	static int argc = 1;
	static char arg0[] = "pakfu-fuzzer";
	static char* argv[] = {arg0, nullptr};
	static QCoreApplication app(argc, argv);
	Q_UNUSED(app);
}

inline QByteArray fuzz_input_bytes(const std::uint8_t* data, std::size_t size) {
	if (!data || size == 0) {
		return {};
	}

	const std::size_t capped = std::min<std::size_t>(
	  size,
	  static_cast<std::size_t>(std::numeric_limits<int>::max()));
	return QByteArray(reinterpret_cast<const char*>(data), static_cast<int>(capped));
}

inline std::unique_ptr<QTemporaryFile> write_fuzz_input_file(const QByteArray& bytes, const QString& suffix) {
	const QString pattern =
	  QDir::temp().filePath(QString("pakfu-fuzz-XXXXXX.%1").arg(suffix));
	auto file = std::make_unique<QTemporaryFile>(pattern);
	file->setAutoRemove(true);
	if (!file->open()) {
		return nullptr;
	}
	if (file->write(bytes) != bytes.size()) {
		return nullptr;
	}
	file->flush();
	file->close();
	return file;
}

template <typename ArchiveT>
inline void exercise_archive_contents(ArchiveT* archive) {
	if (!archive) {
		return;
	}

	const auto& entries = archive->entries();
	QTemporaryDir temp_dir;
	const bool can_extract = temp_dir.isValid();
	const int limit = std::min(entries.size(), 4);
	for (int i = 0; i < limit; ++i) {
		const ArchiveEntry& entry = entries.at(i);
		if (entry.name.isEmpty() || entry.name.endsWith('/')) {
			continue;
		}

		QByteArray bytes;
		archive->read_entry_bytes(entry.name, &bytes, nullptr, 4096);
		if (can_extract) {
			const QString out_path =
			  QDir(temp_dir.path()).filePath(QString("entry-%1.bin").arg(i));
			archive->extract_entry_to_file(entry.name, out_path, nullptr);
		}
	}
}
