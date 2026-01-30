#pragma once

#include <QVector>
#include <QString>
#include <QtGlobal>

struct PakEntry {
  QString name;
  quint32 offset = 0;
  quint32 size = 0;
};

class PakArchive {
public:
  bool load(const QString& path, QString* error);
  bool save_as(const QString& dest_path, QString* error) const;
  static bool write_empty(const QString& dest_path, QString* error);

  bool is_loaded() const { return loaded_; }
  QString path() const { return path_; }
  const QVector<PakEntry>& entries() const { return entries_; }

private:
  bool loaded_ = false;
  QString path_;
  qint64 file_size_ = 0;
  QVector<PakEntry> entries_;
};
