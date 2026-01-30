#pragma once

#include <QVector>
#include <QString>

struct PakEntry {
  QString name;
  quint32 offset = 0;
  quint32 size = 0;
};

class PakArchive {
public:
  bool load(const QString& path, QString* error);
  const QVector<PakEntry>& entries() const { return entries_; }

private:
  QVector<PakEntry> entries_;
};

