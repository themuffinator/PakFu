#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QTemporaryFile>
#include <QVector>
#include <QString>

#include <memory>

#include "archive/archive_entry.h"

class ZipArchive {
public:
  ~ZipArchive();
  bool load(const QString& path, QString* error);

  bool is_loaded() const { return loaded_; }
  QString path() const { return path_; }  // The user-visible archive path (may be encrypted).
  QString readable_zip_path() const { return zip_path_; }  // The on-disk ZIP path used for reading.
  const QVector<ArchiveEntry>& entries() const { return entries_; }

  bool read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes = -1) const;
  bool extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const;

  bool is_quakelive_encrypted_pk3() const { return quakelive_encrypted_pk3_; }

private:
  bool init_from_device(QIODevice* device, qint64 device_size, QString* error);
  bool load_zip_from_file(const QString& file_path, QString* error);
  bool maybe_load_quakelive_encrypted_pk3(const QString& file_path, QString* error);

  static QString normalize_entry_name(QString name);

  bool loaded_ = false;
  bool quakelive_encrypted_pk3_ = false;
  QString path_;
  QString zip_path_;  // The readable ZIP path (may point to a decrypted temp file).
  QVector<ArchiveEntry> entries_;
  QHash<QString, int> index_by_name_;
  struct ZipState;
  struct ZipStateDeleter {
    void operator()(ZipState* p) const noexcept;
  };
  std::unique_ptr<ZipState, ZipStateDeleter> state_;
  std::unique_ptr<QFile> zip_file_;
  std::unique_ptr<QTemporaryFile> decrypted_temp_;
};
