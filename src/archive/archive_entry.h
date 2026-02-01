#pragma once

#include <QString>
#include <QtGlobal>

struct ArchiveEntry {
  QString name;
  quint32 offset = 0;  // Format-specific (e.g. PAK byte offset); may be 0 when unused.
  quint32 size = 0;    // Uncompressed size in bytes when applicable.
  qint64 mtime_utc_secs = -1;  // UTC seconds since epoch, or -1 when unknown.
};

