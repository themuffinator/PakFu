#pragma once

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QVector>

#include "archive/archive_entry.h"
#include "archive/dir_archive.h"
#include "pak/pak_archive.h"
#include "wad/wad_archive.h"
#include "zip/zip_archive.h"

class Archive {
public:
  enum class Format {
    Unknown = 0,
    Directory,
    Pak,
    Wad,
    Zip,
  };

  bool load(const QString& path, QString* error);

  bool is_loaded() const { return loaded_; }
  Format format() const { return format_; }
  QString path() const { return path_; }  // User-selected path.
  QString readable_path() const { return readable_path_; }  // On-disk path used for reading (may be a decrypted temp file).

  bool is_quakelive_encrypted_pk3() const { return quakelive_encrypted_pk3_; }

  const QVector<ArchiveEntry>& entries() const;

  bool read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes = -1) const;
  bool extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const;

private:
  mutable QMutex mutex_;
  Format format_ = Format::Unknown;
  bool loaded_ = false;
  bool quakelive_encrypted_pk3_ = false;
  QString path_;
  QString readable_path_;
  DirArchive dir_;
  PakArchive pak_;
  WadArchive wad_;
  ZipArchive zip_;
};
