#include "resources/resources_archive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "archive/path_safety.h"

namespace {
constexpr quint32 kResourcesMagic = 0xD000000D;
constexpr qint64 kResourcesHeaderSize = 12;

[[nodiscard]] quint32 read_u32_le_from(const char* p) {
  const quint32 b0 = static_cast<quint8>(p[0]);
  const quint32 b1 = static_cast<quint8>(p[1]);
  const quint32 b2 = static_cast<quint8>(p[2]);
  const quint32 b3 = static_cast<quint8>(p[3]);
  return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

[[nodiscard]] quint32 read_u32_be_from(const char* p) {
  const quint32 b0 = static_cast<quint8>(p[0]);
  const quint32 b1 = static_cast<quint8>(p[1]);
  const quint32 b2 = static_cast<quint8>(p[2]);
  const quint32 b3 = static_cast<quint8>(p[3]);
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}
}  // namespace

QString ResourcesArchive::normalize_entry_name(QString name) {
  return normalize_archive_entry_name(std::move(name));
}

const ResourcesArchive::EntryMeta* ResourcesArchive::find_entry(const QString& name) const {
  const QString key = normalize_entry_name(name);
  const int idx = index_by_name_.value(key, -1);
  if (idx < 0 || idx >= meta_by_index_.size()) {
    return nullptr;
  }
  return &meta_by_index_[idx];
}

bool ResourcesArchive::load(const QString& path, QString* error) {
  if (error) {
    error->clear();
  }

  loaded_ = false;
  path_.clear();
  entries_.clear();
  meta_by_index_.clear();
  index_by_name_.clear();

  const QString abs = QFileInfo(path).absoluteFilePath();
  if (abs.isEmpty() || !QFileInfo::exists(abs)) {
    if (error) {
      *error = "Resources file not found.";
    }
    return false;
  }

  QFile file(abs);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open resources file.";
    }
    return false;
  }

  const qint64 file_size = file.size();
  if (file_size < kResourcesHeaderSize) {
    if (error) {
      *error = "Resources header is incomplete.";
    }
    return false;
  }

  const QByteArray header = file.read(static_cast<int>(kResourcesHeaderSize));
  if (header.size() != kResourcesHeaderSize) {
    if (error) {
      *error = "Resources header is incomplete.";
    }
    return false;
  }

  const quint32 magic = read_u32_be_from(header.constData() + 0);
  if (magic != kResourcesMagic) {
    if (error) {
      *error = "Not a Doom 3 BFG resources file (invalid magic).";
    }
    return false;
  }

  const quint32 toc_offset_u = read_u32_be_from(header.constData() + 4);
  const quint32 toc_size_u = read_u32_be_from(header.constData() + 8);
  const qint64 toc_offset = static_cast<qint64>(toc_offset_u);
  const qint64 toc_size = static_cast<qint64>(toc_size_u);
  const qint64 toc_end = toc_offset + toc_size;

  if (toc_offset < kResourcesHeaderSize || toc_size < 8 || toc_end < toc_offset || toc_end > file_size) {
    if (error) {
      *error = "Resources table-of-contents range is invalid.";
    }
    return false;
  }

  if (!file.seek(toc_offset)) {
    if (error) {
      *error = "Unable to seek resources table-of-contents.";
    }
    return false;
  }

  const QByteArray num_files_bytes = file.read(4);
  if (num_files_bytes.size() != 4) {
    if (error) {
      *error = "Unable to read resources table-of-contents header.";
    }
    return false;
  }
  const quint32 num_files_u = read_u32_be_from(num_files_bytes.constData());
  if (num_files_u > 2'000'000) {
    if (error) {
      *error = "Resources entry count is invalid.";
    }
    return false;
  }

  const qint64 toc_payload_size = toc_size - 4;

  entries_.reserve(static_cast<int>(num_files_u));
  meta_by_index_.reserve(static_cast<int>(num_files_u));
  index_by_name_.reserve(static_cast<int>(num_files_u));

  qint64 consumed = 0;
  for (quint32 i = 0; i < num_files_u; ++i) {
    if (consumed + 4 > toc_payload_size) {
      if (error) {
        *error = "Resources table-of-contents is truncated (filename length).";
      }
      return false;
    }
    char len_buf[4]{};
    if (file.read(len_buf, 4) != 4) {
      if (error) {
        *error = "Unable to read resources filename length.";
      }
      return false;
    }
    consumed += 4;
    const quint32 name_len_u = read_u32_le_from(len_buf);
    const qint64 name_len = static_cast<qint64>(name_len_u);
    if (name_len <= 0 || name_len > 1024 * 1024) {
      if (error) {
        *error = "Resources table-of-contents has an invalid filename length.";
      }
      return false;
    }
    if (consumed + name_len + 8 > toc_payload_size) {
      if (error) {
        *error = "Resources table-of-contents is truncated (entry payload).";
      }
      return false;
    }

    const QByteArray raw_name = file.read(name_len);
    if (raw_name.size() != name_len) {
      if (error) {
        *error = "Unable to read resources entry name.";
      }
      return false;
    }
    consumed += name_len;
    QString name = QString::fromUtf8(raw_name);
    if (name.isEmpty()) {
      name = QString::fromLatin1(raw_name);
    }
    name = normalize_entry_name(name);
    if (!is_safe_archive_entry_name(name)) {
      if (error) {
        *error = QString("Resources contains an unsafe entry name: %1").arg(name);
      }
      return false;
    }

    char offs_size_buf[8]{};
    if (file.read(offs_size_buf, 8) != 8) {
      if (error) {
        *error = "Unable to read resources entry location.";
      }
      return false;
    }
    consumed += 8;
    const quint32 offset_u = read_u32_be_from(offs_size_buf + 0);
    const quint32 size_u = read_u32_be_from(offs_size_buf + 4);

    const qint64 offset = static_cast<qint64>(offset_u);
    const qint64 size = static_cast<qint64>(size_u);
    const qint64 end = offset + size;
    if (offset < kResourcesHeaderSize || end < offset || end > toc_offset) {
      if (error) {
        *error = QString("Resources entry is out of bounds: %1").arg(name);
      }
      return false;
    }

    QString unique = name;
    int suffix = 2;
    while (index_by_name_.contains(normalize_entry_name(unique))) {
      unique = QString("%1_%2").arg(name, QString::number(suffix++));
    }

    ArchiveEntry entry;
    entry.name = unique;
    entry.offset = offset_u;
    entry.size = size_u;
    entry.mtime_utc_secs = -1;

    entries_.push_back(entry);
    meta_by_index_.push_back(EntryMeta{offset_u, size_u});
    index_by_name_.insert(normalize_entry_name(unique), entries_.size() - 1);
  }

  if (consumed > toc_payload_size) {
    if (error) {
      *error = "Resources table-of-contents overran expected size.";
    }
    return false;
  }

  loaded_ = true;
  path_ = abs;
  return true;
}

bool ResourcesArchive::read_entry_bytes(const QString& name,
                                        QByteArray* out,
                                        QString* error,
                                        qint64 max_bytes) const {
  if (out) {
    out->clear();
  }
  if (error) {
    error->clear();
  }
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No resources file is loaded.";
    }
    return false;
  }

  const EntryMeta* meta = find_entry(name);
  if (!meta) {
    if (error) {
      *error = QString("Entry not found: %1").arg(name);
    }
    return false;
  }

  QFile file(path_);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open resources file for reading.";
    }
    return false;
  }

  const qint64 end = static_cast<qint64>(meta->offset) + static_cast<qint64>(meta->size);
  if (end < 0 || end > file.size()) {
    if (error) {
      *error = QString("Resources entry is out of bounds: %1").arg(name);
    }
    return false;
  }

  qint64 to_read = static_cast<qint64>(meta->size);
  if (max_bytes >= 0 && to_read > max_bytes) {
    to_read = max_bytes;
  }

  if (!file.seek(static_cast<qint64>(meta->offset))) {
    if (error) {
      *error = QString("Unable to seek entry: %1").arg(name);
    }
    return false;
  }

  QByteArray bytes = file.read(to_read);
  if (bytes.size() != to_read) {
    if (error) {
      *error = QString("Unable to read entry: %1").arg(name);
    }
    return false;
  }

  if (out) {
    *out = std::move(bytes);
  }
  return true;
}

bool ResourcesArchive::extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const {
  if (error) {
    error->clear();
  }
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No resources file is loaded.";
    }
    return false;
  }

  const EntryMeta* meta = find_entry(name);
  if (!meta) {
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

  QFile src(path_);
  if (!src.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open source resources file for reading.";
    }
    return false;
  }

  if (!src.seek(static_cast<qint64>(meta->offset))) {
    if (error) {
      *error = QString("Unable to seek source entry: %1").arg(name);
    }
    return false;
  }

  QSaveFile out_file(dest_path);
  if (!out_file.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = "Unable to create output file.";
    }
    return false;
  }

  constexpr qint64 kChunkSize = 1024 * 1024;
  qint64 remaining = static_cast<qint64>(meta->size);
  QByteArray buf;
  buf.resize(static_cast<int>(qMin<qint64>(kChunkSize, remaining)));
  while (remaining > 0) {
    const qint64 want = qMin<qint64>(remaining, buf.size());
    const qint64 got = src.read(buf.data(), want);
    if (got != want) {
      if (error) {
        *error = QString("Unable to read entry: %1").arg(name);
      }
      return false;
    }
    if (out_file.write(buf.constData(), got) != got) {
      if (error) {
        *error = QString("Unable to write output file: %1").arg(dest_path);
      }
      return false;
    }
    remaining -= got;
  }

  if (!out_file.commit()) {
    if (error) {
      *error = "Unable to finalize output file.";
    }
    return false;
  }

  return true;
}
