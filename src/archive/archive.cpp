#include "archive/archive.h"

#include <QFileInfo>
#include <QMutexLocker>

namespace {
QString file_ext_lower(const QString& name) {
	const QString lower = name.toLower();
	const int dot = lower.lastIndexOf('.');
	return dot >= 0 ? lower.mid(dot + 1) : QString();
}
}  // namespace

const QVector<ArchiveEntry>& Archive::entries() const {
	static const QVector<ArchiveEntry> kEmpty;
	if (!loaded_) {
		return kEmpty;
	}
	switch (format_) {
		case Format::Directory:
			return dir_.entries();
		case Format::Pak:
			return pak_.entries();
		case Format::Wad:
			return wad_.entries();
		case Format::Zip:
			return zip_.entries();
		case Format::Unknown:
			break;
	}
	return kEmpty;
}

bool Archive::load(const QString& path, QString* error) {
	if (error) {
		error->clear();
	}

	loaded_ = false;
	format_ = Format::Unknown;
	quakelive_encrypted_pk3_ = false;
	path_.clear();
	readable_path_.clear();

	const QString abs = QFileInfo(path).absoluteFilePath();
	const QFileInfo info(abs);
	if (abs.isEmpty() || !info.exists()) {
		if (error) {
			*error = "Archive file not found.";
		}
		return false;
	}

	if (info.isDir()) {
		QString err;
		if (dir_.load(abs, &err)) {
			loaded_ = true;
			format_ = Format::Directory;
			path_ = abs;
			readable_path_ = abs;
			return true;
		}
		if (error) {
			*error = err.isEmpty() ? "Unable to open folder." : err;
		}
		return false;
	}

	const QString ext = file_ext_lower(abs);
	const bool looks_zip = (ext == "zip" || ext == "pk3" || ext == "pk4" || ext == "pkz");
	const bool looks_pak = (ext == "pak");
	const bool looks_wad = (ext == "wad");

	QString err;
	if (looks_wad && wad_.load(abs, &err)) {
		loaded_ = true;
		format_ = Format::Wad;
		path_ = abs;
		readable_path_ = abs;
		return true;
	}
	if (looks_zip && zip_.load(abs, &err)) {
		loaded_ = true;
		format_ = Format::Zip;
		path_ = abs;
		readable_path_ = zip_.readable_zip_path().isEmpty() ? abs : zip_.readable_zip_path();
		quakelive_encrypted_pk3_ = zip_.is_quakelive_encrypted_pk3();
		return true;
	}
	if (looks_pak && pak_.load(abs, &err)) {
		loaded_ = true;
		format_ = Format::Pak;
		path_ = abs;
		readable_path_ = abs;
		return true;
	}

	// Heuristic fallback: try both.
	if (!looks_pak && pak_.load(abs, &err)) {
		loaded_ = true;
		format_ = Format::Pak;
		path_ = abs;
		readable_path_ = abs;
		return true;
	}
	if (!looks_wad && wad_.load(abs, &err)) {
		loaded_ = true;
		format_ = Format::Wad;
		path_ = abs;
		readable_path_ = abs;
		return true;
	}
	if (!looks_zip && zip_.load(abs, &err)) {
		loaded_ = true;
		format_ = Format::Zip;
		path_ = abs;
		readable_path_ = zip_.readable_zip_path().isEmpty() ? abs : zip_.readable_zip_path();
		quakelive_encrypted_pk3_ = zip_.is_quakelive_encrypted_pk3();
		return true;
	}

	if (error) {
		*error = err.isEmpty() ? "Unable to load archive." : err;
	}
	return false;
}

bool Archive::read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes) const {
	if (error) {
		error->clear();
	}
	if (out) {
		out->clear();
	}
	QMutexLocker locker(&mutex_);
	if (!loaded_) {
		if (error) {
			*error = "No archive is loaded.";
		}
		return false;
	}
	switch (format_) {
		case Format::Directory:
			return dir_.read_entry_bytes(name, out, error, max_bytes);
		case Format::Pak:
			return pak_.read_entry_bytes(name, out, error, max_bytes);
		case Format::Wad:
			return wad_.read_entry_bytes(name, out, error, max_bytes);
		case Format::Zip:
			return zip_.read_entry_bytes(name, out, error, max_bytes);
		case Format::Unknown:
			break;
	}
	if (error) {
		*error = "Unsupported archive format.";
	}
	return false;
}

bool Archive::extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const {
	if (error) {
		error->clear();
	}
	QMutexLocker locker(&mutex_);
	if (!loaded_) {
		if (error) {
			*error = "No archive is loaded.";
		}
		return false;
	}
	switch (format_) {
		case Format::Directory:
			return dir_.extract_entry_to_file(name, dest_path, error);
		case Format::Pak:
			return pak_.extract_entry_to_file(name, dest_path, error);
		case Format::Wad:
			return wad_.extract_entry_to_file(name, dest_path, error);
		case Format::Zip:
			return zip_.extract_entry_to_file(name, dest_path, error);
		case Format::Unknown:
			break;
	}
	if (error) {
		*error = "Unsupported archive format.";
	}
	return false;
}
