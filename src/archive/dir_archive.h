#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include "archive/archive_entry.h"

class DirArchive {
public:
  bool load(const QString& path, QString* error);

  bool is_loaded() const { return loaded_; }
  QString path() const { return path_; }
  const QVector<ArchiveEntry>& entries() const { return entries_; }

  bool read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes = -1) const;
  bool extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const;

private:
  const ArchiveEntry* find_entry(const QString& name) const;

  bool loaded_ = false;
  QString path_;  // Root directory.
  QVector<ArchiveEntry> entries_;
  QHash<QString, int> index_by_name_;
};

