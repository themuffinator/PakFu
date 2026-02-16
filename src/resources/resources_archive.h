#pragma once

#include <QHash>
#include <QByteArray>
#include <QString>
#include <QVector>

#include "archive/archive_entry.h"

class ResourcesArchive {
public:
  bool load(const QString& path, QString* error);

  bool is_loaded() const { return loaded_; }
  QString path() const { return path_; }

  const QVector<ArchiveEntry>& entries() const { return entries_; }

  bool read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes = -1) const;
  bool extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const;

private:
  struct EntryMeta {
    quint32 offset = 0;
    quint32 size = 0;
  };

  const EntryMeta* find_entry(const QString& name) const;

  static QString normalize_entry_name(QString name);

  bool loaded_ = false;
  QString path_;
  QVector<ArchiveEntry> entries_;
  QVector<EntryMeta> meta_by_index_;
  QHash<QString, int> index_by_name_;
};
