#pragma once

#include <QVector>
#include <QString>
#include <QtGlobal>

#include "archive/archive_entry.h"

class PakArchive {
public:
  bool load(const QString& path, QString* error);
  bool save_as(const QString& dest_path, QString* error) const;
  static bool write_empty(const QString& dest_path, QString* error);

  bool is_loaded() const { return loaded_; }
  QString path() const { return path_; }
  const QVector<ArchiveEntry>& entries() const { return entries_; }
  bool is_sin_archive() const { return sin_archive_; }

  // Reads the bytes of an entry from the loaded PAK.
  // If max_bytes >= 0, reading is limited to that many bytes.
  bool read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes = -1) const;

  // Extracts an entry to a file on disk (overwrites atomically).
  bool extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const;

private:
  const ArchiveEntry* find_entry(const QString& name) const;

  bool loaded_ = false;
  bool sin_archive_ = false;
  int dir_entry_size_ = 64;
  int name_bytes_ = 56;
  QByteArray signature_ = "PACK";
  QString path_;
  qint64 file_size_ = 0;
  QVector<ArchiveEntry> entries_;
};
