#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QtGlobal>
#include <QVector>

class PreviewIoCache {
public:
	struct EntryStamp {
		QString source_path;
		qint64 size = -1;
		qint64 mtime_utc_secs = -1;
		qint64 max_bytes = -1;
		bool is_dir = false;
	};

	struct ExportEntry {
		QString fs_path;
		EntryStamp stamp;
	};

	void clear();
	void clear_bytes();
	void clear_exports();

	[[nodiscard]] static QString make_key(const QString& scope,
	                                      const QString& archive_path,
	                                      bool is_dir,
	                                      qint64 max_bytes = -1);

	[[nodiscard]] bool lookup_bytes(const QString& key, const EntryStamp& stamp, QByteArray* out, bool* cache_hit = nullptr);
	void store_bytes(const QString& key, const EntryStamp& stamp, const QByteArray& bytes);

	[[nodiscard]] bool lookup_export(const QString& key, const EntryStamp& stamp, QString* out_fs_path, bool* cache_hit = nullptr);
	void store_export(const QString& key, const EntryStamp& stamp, const QString& fs_path);
	void remove_export(const QString& key);

	[[nodiscard]] int byte_entry_count() const;
	[[nodiscard]] int export_entry_count() const;

private:
	struct ByteEntry {
		QByteArray bytes;
		EntryStamp stamp;
	};

	[[nodiscard]] static bool stamps_match(const EntryStamp& a, const EntryStamp& b);
	void touch_key(QVector<QString>* order, const QString& key);
	void trim_bytes_locked();
	void trim_exports_locked();

	mutable QMutex mutex_;
	QHash<QString, ByteEntry> byte_entries_;
	QVector<QString> byte_order_;
	QHash<QString, ExportEntry> export_entries_;
	QVector<QString> export_order_;
	int max_byte_entries_ = 24;
	int max_export_entries_ = 16;
	qint64 max_single_byte_entry_ = 64LL * 1024LL * 1024LL;
};
