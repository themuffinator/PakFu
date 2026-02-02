#pragma once

#include <QHash>
#include <QByteArray>
#include <QString>
#include <QVector>

#include "archive/archive_entry.h"

class WadArchive {
public:
  bool load(const QString& path, QString* error);

  bool is_loaded() const { return loaded_; }
  QString path() const { return path_; }
  bool is_wad3() const { return wad3_; }

  const QVector<ArchiveEntry>& entries() const { return entries_; }

  bool read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes = -1) const;
  bool extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const;

private:
  struct LumpMeta {
    quint32 offset = 0;
    quint32 disk_size = 0;
    quint8 type = 0;
    quint8 compression = 0;
  };

  const LumpMeta* find_lump(const QString& name) const;

  static QString normalize_entry_name(QString name);
  static QString clean_lump_base_name(const QByteArray& name16);

  bool loaded_ = false;
  bool wad3_ = false;
  QString path_;
  QVector<ArchiveEntry> entries_;
  QVector<LumpMeta> meta_by_index_;
  QHash<QString, int> index_by_name_;
};

