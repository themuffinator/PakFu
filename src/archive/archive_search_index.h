#pragma once

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "archive/archive_entry.h"

class ArchiveSearchIndex {
public:
	struct OverlayFile {
		QString path;
		QString source_path;
		quint32 size = 0;
		qint64 mtime_utc_secs = -1;
	};

	struct BuildInput {
		QVector<ArchiveEntry> archive_entries;
		QVector<OverlayFile> overlay_files;
		QSet<QString> virtual_dirs;
		QSet<QString> deleted_files;
		QSet<QString> deleted_dir_prefixes;
		qint64 fallback_mtime_utc_secs = -1;
		QString scope_label;
	};

	struct Item {
		QString path;
		QString source_path;
		QString scope_label;
		QStringList dependency_hints;
		quint32 size = 0;
		qint64 mtime_utc_secs = -1;
		bool is_dir = false;
		bool is_added = false;
		bool is_overridden = false;
	};

	void rebuild(const BuildInput& input);
	void clear();

	[[nodiscard]] bool is_empty() const { return items_.isEmpty(); }
	[[nodiscard]] const QVector<Item>& items() const { return items_; }
	[[nodiscard]] QVector<Item> search(const QString& query, int max_results = 1000) const;

private:
	QVector<Item> items_;
};
