#include "archive/dir_archive.h"

#include <algorithm>
#include <limits>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "archive/path_safety.h"

namespace {
QString entry_fs_path(const QString& root_dir, const QString& entry_name) {
  if (root_dir.isEmpty() || entry_name.isEmpty()) {
    return {};
  }
  return QDir(root_dir).filePath(QString(entry_name).replace('/', QDir::separator()));
}
}  // namespace

bool DirArchive::load(const QString& path, QString* error) {
  if (error) {
    error->clear();
  }

  loaded_ = false;
  path_.clear();
  entries_.clear();
  index_by_name_.clear();

  const QFileInfo root_info(path);
  const QString abs = root_info.absoluteFilePath();
  if (abs.isEmpty() || !root_info.exists() || !root_info.isDir()) {
    if (error) {
      *error = "Folder not found.";
    }
    return false;
  }

  QDir root(abs);

  QVector<ArchiveEntry> entries;
  entries.reserve(2048);

  QDirIterator it(abs, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo fi = it.fileInfo();
    if (!fi.isFile()) {
      continue;
    }

    QString rel = root.relativeFilePath(fi.absoluteFilePath());
    rel = normalize_archive_entry_name(rel);
    if (rel.isEmpty()) {
      continue;
    }
    if (!is_safe_archive_entry_name(rel)) {
      continue;
    }

    const qint64 size64 = fi.size();
    if (size64 < 0 || size64 > std::numeric_limits<quint32>::max()) {
      if (error) {
        *error = QString("File is too large: %1").arg(fi.absoluteFilePath());
      }
      return false;
    }

    ArchiveEntry e;
    e.name = rel;
    e.offset = 0;
    e.size = static_cast<quint32>(size64);
    e.mtime_utc_secs = fi.lastModified().toUTC().toSecsSinceEpoch();
    entries.push_back(std::move(e));
  }

  std::sort(entries.begin(), entries.end(), [](const ArchiveEntry& a, const ArchiveEntry& b) {
    return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
  });

  QHash<QString, int> index;
  index.reserve(entries.size());
  for (int i = 0; i < entries.size(); ++i) {
    index.insert(entries[i].name, i);
  }

  loaded_ = true;
  path_ = abs;
  entries_ = std::move(entries);
  index_by_name_ = std::move(index);
  return true;
}

const ArchiveEntry* DirArchive::find_entry(const QString& name) const {
  const QString normalized = normalize_archive_entry_name(name);
  if (normalized.isEmpty()) {
    return nullptr;
  }
  auto it = index_by_name_.find(normalized);
  if (it == index_by_name_.end()) {
    return nullptr;
  }
  const int idx = it.value();
  if (idx < 0 || idx >= entries_.size()) {
    return nullptr;
  }
  return &entries_[idx];
}

bool DirArchive::read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes) const {
  if (error) {
    error->clear();
  }
  if (out) {
    out->clear();
  }
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No folder is loaded.";
    }
    return false;
  }

  const ArchiveEntry* entry = find_entry(name);
  if (!entry) {
    if (error) {
      *error = QString("Entry not found: %1").arg(name);
    }
    return false;
  }

  const QString src_path = entry_fs_path(path_, entry->name);
  if (src_path.isEmpty()) {
    if (error) {
      *error = "Invalid entry path.";
    }
    return false;
  }

  QFile src(src_path);
  if (!src.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = QString("Unable to open file: %1").arg(src_path);
    }
    return false;
  }

  qint64 to_read = entry->size;
  if (max_bytes >= 0 && to_read > max_bytes) {
    to_read = max_bytes;
  }

  const QByteArray bytes = src.read(to_read);
  if (bytes.size() != to_read) {
    if (error) {
      *error = QString("Unable to read file: %1").arg(src_path);
    }
    return false;
  }

  if (out) {
    *out = bytes;
  }
  return true;
}

bool DirArchive::extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const {
  if (error) {
    error->clear();
  }
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No folder is loaded.";
    }
    return false;
  }

  const ArchiveEntry* entry = find_entry(name);
  if (!entry) {
    if (error) {
      *error = QString("Entry not found: %1").arg(name);
    }
    return false;
  }

  const QFileInfo out_info(dest_path);
  if (!out_info.dir().exists()) {
    QDir d(out_info.dir().absolutePath());
    if (!d.mkpath(".")) {
      if (error) {
        *error = QString("Unable to create output directory: %1").arg(out_info.dir().absolutePath());
      }
      return false;
    }
  }

  const QString src_path = entry_fs_path(path_, entry->name);
  if (src_path.isEmpty()) {
    if (error) {
      *error = "Invalid entry path.";
    }
    return false;
  }

  QFile src(src_path);
  if (!src.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = QString("Unable to open file: %1").arg(src_path);
    }
    return false;
  }

  QSaveFile out(dest_path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QString("Unable to create output file: %1").arg(dest_path);
    }
    return false;
  }

  QByteArray buffer;
  buffer.resize(1 << 16);
  for (;;) {
    const qint64 got = src.read(buffer.data(), buffer.size());
    if (got < 0) {
      if (error) {
        *error = QString("Unable to read file: %1").arg(src_path);
      }
      return false;
    }
    if (got == 0) {
      break;
    }
    if (out.write(buffer.constData(), got) != got) {
      if (error) {
        *error = QString("Unable to write output file: %1").arg(dest_path);
      }
      return false;
    }
  }

  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize output file: %1").arg(dest_path);
    }
    return false;
  }

  return true;
}

