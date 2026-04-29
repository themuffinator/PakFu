#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstring>
#include <limits>
#include <memory>
#include <utility>

#include "archive/archive_session.h"
#include "foundation/performance_metrics.h"
#include "pak/pak_archive.h"
#include "ui/preview_io_cache.h"

namespace {

void fail_message(const QString& message) {
	QTextStream(stderr) << message << '\n';
}

void write_u32_le(QByteArray* bytes, int offset, quint32 value) {
	(*bytes)[offset + 0] = static_cast<char>(value & 0xff);
	(*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
	(*bytes)[offset + 2] = static_cast<char>((value >> 16) & 0xff);
	(*bytes)[offset + 3] = static_cast<char>((value >> 24) & 0xff);
}

bool write_file(const QString& path, const QByteArray& bytes, QString* error) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if (error) {
			*error = QString("Unable to write fixture file: %1").arg(path);
		}
		return false;
	}
	if (file.write(bytes) != bytes.size()) {
		if (error) {
			*error = QString("Unable to write fixture bytes: %1").arg(path);
		}
		return false;
	}
	return true;
}

bool write_pak_fixture(const QString& path, const QVector<QPair<QString, QByteArray>>& files, QString* error) {
	QSaveFile out(path);
	if (!out.open(QIODevice::WriteOnly)) {
		if (error) {
			*error = QString("Unable to create PAK fixture: %1").arg(path);
		}
		return false;
	}

	QByteArray header(12, '\0');
	header[0] = 'P';
	header[1] = 'A';
	header[2] = 'C';
	header[3] = 'K';
	if (out.write(header) != header.size()) {
		if (error) {
			*error = "Unable to write PAK header.";
		}
		return false;
	}

	struct DirEntry {
		QString name;
		quint32 offset = 0;
		quint32 size = 0;
	};
	QVector<DirEntry> dir_entries;
	dir_entries.reserve(files.size());

	for (const auto& item : files) {
		const qint64 offset = out.pos();
		if (offset < 0 || offset > std::numeric_limits<quint32>::max() ||
		    item.second.size() > std::numeric_limits<quint32>::max()) {
			if (error) {
				*error = "PAK fixture exceeds format limits.";
			}
			return false;
		}
		if (out.write(item.second) != item.second.size()) {
			if (error) {
				*error = QString("Unable to write PAK fixture entry: %1").arg(item.first);
			}
			return false;
		}
		dir_entries.push_back(DirEntry{item.first, static_cast<quint32>(offset), static_cast<quint32>(item.second.size())});
	}

	const qint64 dir_offset = out.pos();
	const qint64 dir_length = static_cast<qint64>(dir_entries.size()) * 64;
	if (dir_offset < 0 || dir_offset > std::numeric_limits<quint32>::max() ||
	    dir_length > std::numeric_limits<quint32>::max()) {
		if (error) {
			*error = "PAK fixture directory exceeds format limits.";
		}
		return false;
	}

	QByteArray dir(static_cast<int>(dir_length), '\0');
	for (int i = 0; i < dir_entries.size(); ++i) {
		const DirEntry& entry = dir_entries[i];
		const QByteArray name = entry.name.toLatin1();
		if (name.size() > 56) {
			if (error) {
				*error = QString("PAK fixture entry name is too long: %1").arg(entry.name);
			}
			return false;
		}
		const int base = i * 64;
		std::memcpy(dir.data() + base, name.constData(), static_cast<size_t>(name.size()));
		write_u32_le(&dir, base + 56, entry.offset);
		write_u32_le(&dir, base + 60, entry.size);
	}
	if (out.write(dir) != dir.size()) {
		if (error) {
			*error = "Unable to write PAK fixture directory.";
		}
		return false;
	}

	write_u32_le(&header, 4, static_cast<quint32>(dir_offset));
	write_u32_le(&header, 8, static_cast<quint32>(dir_length));
	if (!out.seek(0) || out.write(header) != header.size()) {
		if (error) {
			*error = "Unable to patch PAK fixture header.";
		}
		return false;
	}

	if (!out.commit()) {
		if (error) {
			*error = "Unable to finalize PAK fixture.";
		}
		return false;
	}
	return true;
}

bool test_pak_index_preserves_first_duplicate(const QString& root, QString* error) {
	const QString pak_path = QDir(root).filePath("indexed.pak");
	const QVector<QPair<QString, QByteArray>> files = {
		{"dir/alpha.txt", "first"},
		{"dir/beta.txt", "beta"},
		{"dir/alpha.txt", "second"},
	};
	if (!write_pak_fixture(pak_path, files, error)) {
		return false;
	}

	PakArchive pak;
	if (!pak.load(pak_path, error)) {
		return false;
	}

	QByteArray bytes;
	if (!pak.read_entry_bytes("/dir\\alpha.txt", &bytes, error)) {
		return false;
	}
	if (bytes != QByteArray("first")) {
		if (error) {
			*error = "PAK indexed lookup did not preserve first duplicate entry behavior.";
		}
		return false;
	}
	return true;
}

bool test_archive_session_current_archive(const QString& root, QString* error) {
	const QString primary_path = QDir(root).filePath("primary.pak");
	const QString mounted_path = QDir(root).filePath("mounted.pak");
	if (!write_pak_fixture(primary_path, {{"outer.txt", "outer"}}, error) ||
	    !write_pak_fixture(mounted_path, {{"inner.txt", "inner"}}, error)) {
		return false;
	}

	ArchiveSession session;
	if (!session.primary_archive().load(primary_path, error)) {
		return false;
	}

	QByteArray bytes;
	if (!session.current_archive().read_entry_bytes("outer.txt", &bytes, error) || bytes != QByteArray("outer")) {
		if (error && error->isEmpty()) {
			*error = "ArchiveSession did not expose the primary archive.";
		}
		return false;
	}

	auto mounted = std::make_unique<Archive>();
	if (!mounted->load(mounted_path, error)) {
		return false;
	}

	ArchiveSession::MountedArchiveLayer layer;
	layer.archive = std::move(mounted);
	layer.mount_name = "mounted.pak";
	layer.mount_fs_path = mounted_path;
	session.push_mounted_archive(std::move(layer));

	bytes.clear();
	if (!session.current_archive().read_entry_bytes("inner.txt", &bytes, error) || bytes != QByteArray("inner")) {
		if (error && error->isEmpty()) {
			*error = "ArchiveSession did not expose the mounted archive.";
		}
		return false;
	}

	session.pop_mounted_archive();
	bytes.clear();
	if (!session.current_archive().read_entry_bytes("outer.txt", &bytes, error) || bytes != QByteArray("outer")) {
		if (error && error->isEmpty()) {
			*error = "ArchiveSession did not restore the primary archive after unmount.";
		}
		return false;
	}

	return true;
}

bool test_preview_io_cache(const QString& root, QString* error) {
	PreviewIoCache cache;
	PreviewIoCache::EntryStamp stamp;
	stamp.source_path = "archive";
	stamp.size = 5;
	stamp.mtime_utc_secs = 10;
	stamp.max_bytes = 5;
	stamp.is_dir = false;

	const QString key = PreviewIoCache::make_key("scope", "a.txt", false, 5);
	cache.store_bytes(key, stamp, "hello");

	QByteArray bytes;
	bool hit = false;
	if (!cache.lookup_bytes(key, stamp, &bytes, &hit) || !hit || bytes != QByteArray("hello")) {
		if (error) {
			*error = "PreviewIoCache byte lookup did not hit.";
		}
		return false;
	}

	PreviewIoCache::EntryStamp changed = stamp;
	changed.mtime_utc_secs = 11;
	if (cache.lookup_bytes(key, changed, &bytes, &hit) || hit) {
		if (error) {
			*error = "PreviewIoCache accepted stale byte metadata.";
		}
		return false;
	}

	const QString export_path = QDir(root).filePath("exported.bin");
	if (!write_file(export_path, "exported", error)) {
		return false;
	}
	cache.store_export(key, stamp, export_path);
	QString cached_path;
	if (!cache.lookup_export(key, stamp, &cached_path, &hit) || !hit || cached_path != export_path) {
		if (error) {
			*error = "PreviewIoCache export lookup did not hit.";
		}
		return false;
	}

	return true;
}

bool test_performance_profile_helpers(QString* error) {
	QStringList steps;
	PakFu::Metrics::add_profile_step(&steps, "read", 0, true);
	const QString profile = PakFu::Metrics::profile_text(steps, 3);
	if (!profile.contains("read <1 ms (cached)") || !profile.contains("total 3 ms")) {
		if (error) {
			*error = "Performance profile helper text changed unexpectedly.";
		}
		return false;
	}
	return true;
}

}  // namespace

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	Q_UNUSED(app);

	QTemporaryDir temp;
	if (!temp.isValid()) {
		fail_message("Unable to create temporary test directory.");
		return 1;
	}

	QString error;
	if (!test_pak_index_preserves_first_duplicate(temp.path(), &error) ||
	    !test_archive_session_current_archive(temp.path(), &error) ||
	    !test_preview_io_cache(temp.path(), &error) ||
	    !test_performance_profile_helpers(&error)) {
		fail_message(error.isEmpty() ? "Foundation phase test failed." : error);
		return 1;
	}

	return 0;
}
