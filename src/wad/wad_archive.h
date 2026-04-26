#pragma once

#include <QHash>
#include <QByteArray>
#include <QString>
#include <QVector>

#include "archive/archive_entry.h"

class WadArchive {
public:
  struct WriteEntry {
    QString entry_name;
    QString source_path;
    bool from_source_wad = false;
  };

  struct WritePlan {
    // Optional WAD path used by WriteEntry::from_source_wad entries.
    QString source_wad_path;
    QVector<WriteEntry> entries;
  };

  bool load(const QString& path, QString* error);
  static bool derive_wad2_lump_name(const QString& entry_name_in, QString* out_lump_name, QString* error);
  static bool write_wad2(const QString& dest_path, const WritePlan& plan, QString* error);

  bool is_loaded() const { return loaded_; }
  QString path() const { return path_; }
  bool is_wad3() const { return wad3_; }
  bool is_doom_wad() const { return doom_wad_; }

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
  bool doom_wad_ = false;
  QString path_;
  QVector<ArchiveEntry> entries_;
  QVector<LumpMeta> meta_by_index_;
  QHash<QString, int> index_by_name_;
};
