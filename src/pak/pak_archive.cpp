#include "pak_archive.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>

namespace {
constexpr int kPakHeaderSize = 12;
constexpr int kPakDirEntrySize = 64;
constexpr int kPakNameBytes = 56;

bool read_u32_le(const QByteArray& bytes, int offset, quint32* out) {
  if (!out || offset < 0 || offset + 4 > bytes.size()) {
    return false;
  }
  const auto b0 = static_cast<quint8>(bytes[offset + 0]);
  const auto b1 = static_cast<quint8>(bytes[offset + 1]);
  const auto b2 = static_cast<quint8>(bytes[offset + 2]);
  const auto b3 = static_cast<quint8>(bytes[offset + 3]);
  *out = (static_cast<quint32>(b0)) |
         (static_cast<quint32>(b1) << 8) |
         (static_cast<quint32>(b2) << 16) |
         (static_cast<quint32>(b3) << 24);
  return true;
}

void write_u32_le(QByteArray* bytes, int offset, quint32 value) {
  if (!bytes || offset < 0 || offset + 4 > bytes->size()) {
    return;
  }
  (*bytes)[offset + 0] = static_cast<char>(value & 0xFF);
  (*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
  (*bytes)[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
  (*bytes)[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

QString sanitize_entry_name(const QByteArray& raw) {
  int end = raw.indexOf('\0');
  QByteArray trimmed = end >= 0 ? raw.left(end) : raw;
  QString name = QString::fromLatin1(trimmed);
  // Normalize to forward slashes for in-archive paths.
  name.replace('\\', '/');
  while (name.startsWith('/')) {
    name.remove(0, 1);
  }
  name = QDir::cleanPath(name);
  name.replace('\\', '/');
  return name.trimmed();
}

bool is_safe_entry_name(const QString& name) {
  if (name.isEmpty()) {
    return false;
  }
  if (name.contains('\\') || name.contains(':')) {
    return false;
  }
  if (name.startsWith('/') || name.startsWith("./") || name.startsWith("../")) {
    return false;
  }
  const QStringList parts = name.split('/', Qt::SkipEmptyParts);
  for (const QString& p : parts) {
    if (p == "." || p == "..") {
      return false;
    }
  }
  return true;
}
}  // namespace

bool PakArchive::load(const QString& path, QString* error) {
  loaded_ = false;
  path_.clear();
  file_size_ = 0;
  entries_.clear();

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open PAK file.";
    }
    return false;
  }

  file_size_ = file.size();

  QByteArray header = file.read(kPakHeaderSize);
  if (header.size() != kPakHeaderSize) {
    if (error) {
      *error = "PAK file is too small.";
    }
    return false;
  }

  if (header[0] != 'P' || header[1] != 'A' || header[2] != 'C' || header[3] != 'K') {
    if (error) {
      *error = "Not a valid Quake PAK (missing PACK header).";
    }
    return false;
  }

  quint32 dir_offset = 0;
  quint32 dir_length = 0;
  if (!read_u32_le(header, 4, &dir_offset) || !read_u32_le(header, 8, &dir_length)) {
    if (error) {
      *error = "Unable to read PAK header.";
    }
    return false;
  }

  if (dir_length % kPakDirEntrySize != 0) {
    if (error) {
      *error = "PAK directory has an invalid size.";
    }
    return false;
  }

  if (static_cast<qint64>(dir_offset) + static_cast<qint64>(dir_length) > file_size_) {
    if (error) {
      *error = "PAK directory extends past end of file.";
    }
    return false;
  }

  if (!file.seek(dir_offset)) {
    if (error) {
      *error = "PAK directory offset is invalid.";
    }
    return false;
  }

  const int count = static_cast<int>(dir_length / kPakDirEntrySize);
  constexpr int kMaxEntries = 1'000'000;
  if (count > kMaxEntries) {
    if (error) {
      *error = "PAK directory is too large.";
    }
    return false;
  }

  entries_.reserve(count);

  for (int i = 0; i < count; ++i) {
    const QByteArray entry_bytes = file.read(kPakDirEntrySize);
    if (entry_bytes.size() != kPakDirEntrySize) {
      if (error) {
        *error = "Unable to read PAK directory.";
      }
      return false;
    }

    const QByteArray raw_name = entry_bytes.left(kPakNameBytes);
    quint32 offset = 0;
    quint32 size = 0;
    if (!read_u32_le(entry_bytes, kPakNameBytes, &offset) ||
        !read_u32_le(entry_bytes, kPakNameBytes + 4, &size)) {
      if (error) {
        *error = "Unable to read PAK directory entry.";
      }
      return false;
    }

    const QString name = sanitize_entry_name(raw_name);
    if (!is_safe_entry_name(name)) {
      if (error) {
        *error = QString("PAK contains an unsafe entry name: %1").arg(name);
      }
      return false;
    }

    const qint64 end = static_cast<qint64>(offset) + static_cast<qint64>(size);
    if (end < 0 || end > file_size_) {
      if (error) {
        *error = QString("PAK entry extends past end of file: %1").arg(name);
      }
      return false;
    }

    PakEntry entry;
    entry.name = name;
    entry.offset = offset;
    entry.size = size;
    entries_.push_back(std::move(entry));
  }

  loaded_ = true;
  path_ = QFileInfo(path).absoluteFilePath();
  return true;
}

bool PakArchive::write_empty(const QString& dest_path, QString* error) {
  QSaveFile out(dest_path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = "Unable to create PAK file.";
    }
    return false;
  }

  QByteArray header(kPakHeaderSize, '\0');
  header[0] = 'P';
  header[1] = 'A';
  header[2] = 'C';
  header[3] = 'K';
  write_u32_le(&header, 4, static_cast<quint32>(kPakHeaderSize));
  write_u32_le(&header, 8, 0);

  if (out.write(header) != header.size()) {
    if (error) {
      *error = "Unable to write PAK header.";
    }
    return false;
  }

  if (!out.commit()) {
    if (error) {
      *error = "Unable to finalize PAK file.";
    }
    return false;
  }

  return true;
}

bool PakArchive::save_as(const QString& dest_path, QString* error) const {
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No PAK is loaded.";
    }
    return false;
  }

  QFile src(path_);
  if (!src.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open source PAK for reading.";
    }
    return false;
  }

  const qint64 src_size = src.size();

  QSaveFile out(dest_path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = "Unable to create destination PAK.";
    }
    return false;
  }

  QByteArray header(kPakHeaderSize, '\0');
  header[0] = 'P';
  header[1] = 'A';
  header[2] = 'C';
  header[3] = 'K';
  // Offsets will be patched later.
  if (out.write(header) != header.size()) {
    if (error) {
      *error = "Unable to write PAK header.";
    }
    return false;
  }

  QVector<PakEntry> new_entries;
  new_entries.reserve(entries_.size());

  constexpr qint64 kChunk = 1 << 16;
  QByteArray buffer;
  buffer.resize(static_cast<int>(kChunk));

  for (const PakEntry& e : entries_) {
    if (!is_safe_entry_name(e.name)) {
      if (error) {
        *error = QString("Refusing to save unsafe entry: %1").arg(e.name);
      }
      return false;
    }

    const QByteArray name_bytes = e.name.toLatin1();
    if (name_bytes.size() > kPakNameBytes) {
      if (error) {
        *error = QString("PAK entry name is too long: %1").arg(e.name);
      }
      return false;
    }

    const qint64 end = static_cast<qint64>(e.offset) + static_cast<qint64>(e.size);
    if (end < 0 || end > src_size) {
      if (error) {
        *error = QString("PAK entry is out of bounds: %1").arg(e.name);
      }
      return false;
    }

    const qint64 out_offset64 = out.pos();
    if (out_offset64 < 0 || out_offset64 > std::numeric_limits<quint32>::max()) {
      if (error) {
        *error = "PAK output exceeds format limits.";
      }
      return false;
    }
    quint32 out_offset = static_cast<quint32>(out_offset64);

    if (!src.seek(static_cast<qint64>(e.offset))) {
      if (error) {
        *error = QString("Unable to seek source entry: %1").arg(e.name);
      }
      return false;
    }

    quint32 remaining = e.size;
    while (remaining > 0) {
      const int to_read = static_cast<int>(std::min<quint32>(remaining, static_cast<quint32>(buffer.size())));
      const qint64 got = src.read(buffer.data(), to_read);
      if (got <= 0) {
        if (error) {
          *error = QString("Unable to read source entry: %1").arg(e.name);
        }
        return false;
      }
      if (out.write(buffer.constData(), got) != got) {
        if (error) {
          *error = QString("Unable to write destination entry: %1").arg(e.name);
        }
        return false;
      }
      remaining -= static_cast<quint32>(got);
    }

    PakEntry out_entry;
    out_entry.name = e.name;
    out_entry.offset = out_offset;
    out_entry.size = e.size;
    new_entries.push_back(out_entry);
  }

  const qint64 dir_offset64 = out.pos();
  if (dir_offset64 < 0 || dir_offset64 > std::numeric_limits<quint32>::max()) {
    if (error) {
      *error = "PAK output exceeds format limits.";
    }
    return false;
  }
  const quint32 dir_offset = static_cast<quint32>(dir_offset64);

  const qint64 dir_length64 = static_cast<qint64>(new_entries.size()) * kPakDirEntrySize;
  if (dir_length64 < 0 || dir_length64 > std::numeric_limits<quint32>::max()) {
    if (error) {
      *error = "PAK directory exceeds format limits.";
    }
    return false;
  }
  const quint32 dir_length = static_cast<quint32>(dir_length64);

  QByteArray dir;
  dir.resize(static_cast<int>(dir_length));
  dir.fill('\0');

  for (int i = 0; i < new_entries.size(); ++i) {
    const PakEntry& e = new_entries[i];
    const int base = i * kPakDirEntrySize;
    const QByteArray name = e.name.toLatin1();
    std::memcpy(dir.data() + base, name.constData(), static_cast<size_t>(name.size()));
    write_u32_le(&dir, base + kPakNameBytes, e.offset);
    write_u32_le(&dir, base + kPakNameBytes + 4, e.size);
  }

  if (out.write(dir) != dir.size()) {
    if (error) {
      *error = "Unable to write PAK directory.";
    }
    return false;
  }

  // Close the source file before committing in case we're overwriting in-place.
  src.close();

  // Patch header with directory metadata.
  write_u32_le(&header, 4, dir_offset);
  write_u32_le(&header, 8, dir_length);
  if (!out.seek(0) || out.write(header) != header.size()) {
    if (error) {
      *error = "Unable to update PAK header.";
    }
    return false;
  }

  if (!out.commit()) {
    if (error) {
      *error = "Unable to finalize destination PAK.";
    }
    return false;
  }

  return true;
}
