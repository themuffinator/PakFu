#include "ui/preview_io_cache.h"

#include <QFileInfo>
#include <QMutexLocker>

void PreviewIoCache::clear() {
	QMutexLocker locker(&mutex_);
	byte_entries_.clear();
	byte_order_.clear();
	export_entries_.clear();
	export_order_.clear();
}

void PreviewIoCache::clear_bytes() {
	QMutexLocker locker(&mutex_);
	byte_entries_.clear();
	byte_order_.clear();
}

void PreviewIoCache::clear_exports() {
	QMutexLocker locker(&mutex_);
	export_entries_.clear();
	export_order_.clear();
}

QString PreviewIoCache::make_key(const QString& scope, const QString& archive_path, bool is_dir, qint64 max_bytes) {
	return QString("%1|%2|%3|%4")
		.arg(scope, is_dir ? QString("dir") : QString("file"), archive_path, QString::number(max_bytes));
}

bool PreviewIoCache::stamps_match(const EntryStamp& a, const EntryStamp& b) {
	return a.source_path == b.source_path &&
	       a.size == b.size &&
	       a.mtime_utc_secs == b.mtime_utc_secs &&
	       a.max_bytes == b.max_bytes &&
	       a.is_dir == b.is_dir;
}

bool PreviewIoCache::lookup_bytes(const QString& key, const EntryStamp& stamp, QByteArray* out, bool* cache_hit) {
	if (cache_hit) {
		*cache_hit = false;
	}
	if (out) {
		out->clear();
	}

	QMutexLocker locker(&mutex_);
	const auto it = byte_entries_.find(key);
	if (it == byte_entries_.end() || !stamps_match(it->stamp, stamp)) {
		return false;
	}
	if (out) {
		*out = it->bytes;
	}
	touch_key(&byte_order_, key);
	if (cache_hit) {
		*cache_hit = true;
	}
	return true;
}

void PreviewIoCache::store_bytes(const QString& key, const EntryStamp& stamp, const QByteArray& bytes) {
	if (key.isEmpty()) {
		return;
	}
	if (bytes.size() > max_single_byte_entry_) {
		return;
	}

	QMutexLocker locker(&mutex_);
	byte_entries_.insert(key, ByteEntry{bytes, stamp});
	touch_key(&byte_order_, key);
	trim_bytes_locked();
}

bool PreviewIoCache::lookup_export(const QString& key, const EntryStamp& stamp, QString* out_fs_path, bool* cache_hit) {
	if (cache_hit) {
		*cache_hit = false;
	}
	if (out_fs_path) {
		out_fs_path->clear();
	}

	QMutexLocker locker(&mutex_);
	const auto it = export_entries_.find(key);
	if (it == export_entries_.end() || !stamps_match(it->stamp, stamp)) {
		return false;
	}

	const QFileInfo info(it->fs_path);
	const bool exists = stamp.is_dir ? info.isDir() : info.isFile();
	if (!exists) {
		export_entries_.erase(it);
		export_order_.removeAll(key);
		return false;
	}

	if (out_fs_path) {
		*out_fs_path = it->fs_path;
	}
	touch_key(&export_order_, key);
	if (cache_hit) {
		*cache_hit = true;
	}
	return true;
}

void PreviewIoCache::store_export(const QString& key, const EntryStamp& stamp, const QString& fs_path) {
	if (key.isEmpty() || fs_path.isEmpty()) {
		return;
	}

	QMutexLocker locker(&mutex_);
	export_entries_.insert(key, ExportEntry{fs_path, stamp});
	touch_key(&export_order_, key);
	trim_exports_locked();
}

void PreviewIoCache::remove_export(const QString& key) {
	QMutexLocker locker(&mutex_);
	export_entries_.remove(key);
	export_order_.removeAll(key);
}

int PreviewIoCache::byte_entry_count() const {
	QMutexLocker locker(&mutex_);
	return byte_entries_.size();
}

int PreviewIoCache::export_entry_count() const {
	QMutexLocker locker(&mutex_);
	return export_entries_.size();
}

void PreviewIoCache::touch_key(QVector<QString>* order, const QString& key) {
	if (!order) {
		return;
	}
	order->removeAll(key);
	order->push_back(key);
}

void PreviewIoCache::trim_bytes_locked() {
	while (byte_entries_.size() > max_byte_entries_ && !byte_order_.isEmpty()) {
		const QString oldest = byte_order_.takeFirst();
		byte_entries_.remove(oldest);
	}
}

void PreviewIoCache::trim_exports_locked() {
	while (export_entries_.size() > max_export_entries_ && !export_order_.isEmpty()) {
		const QString oldest = export_order_.takeFirst();
		export_entries_.remove(oldest);
	}
}
