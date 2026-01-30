#include "pak_archive.h"

#include <QFile>

namespace {
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

QString sanitize_entry_name(const QByteArray& raw) {
  int end = raw.indexOf('\0');
  QByteArray trimmed = end >= 0 ? raw.left(end) : raw;
  QString name = QString::fromLatin1(trimmed);
  // Normalize to forward slashes for in-archive paths.
  name.replace('\\', '/');
  while (name.startsWith('/')) {
    name.remove(0, 1);
  }
  return name.trimmed();
}
}  // namespace

bool PakArchive::load(const QString& path, QString* error) {
  entries_.clear();

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open PAK file.";
    }
    return false;
  }

  QByteArray header = file.read(12);
  if (header.size() != 12) {
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

  if (dir_length % 64 != 0) {
    if (error) {
      *error = "PAK directory has an invalid size.";
    }
    return false;
  }

  if (!file.seek(dir_offset)) {
    if (error) {
      *error = "PAK directory offset is invalid.";
    }
    return false;
  }

  QByteArray dir = file.read(static_cast<qint64>(dir_length));
  if (dir.size() != static_cast<int>(dir_length)) {
    if (error) {
      *error = "Unable to read PAK directory.";
    }
    return false;
  }

  const int count = static_cast<int>(dir_length / 64);
  entries_.reserve(count);

  for (int i = 0; i < count; ++i) {
    const int base = i * 64;
    const QByteArray raw_name = dir.mid(base, 56);
    quint32 offset = 0;
    quint32 size = 0;
    if (!read_u32_le(dir, base + 56, &offset) || !read_u32_le(dir, base + 60, &size)) {
      continue;
    }
    const QString name = sanitize_entry_name(raw_name);
    if (name.isEmpty()) {
      continue;
    }
    PakEntry entry;
    entry.name = name;
    entry.offset = offset;
    entry.size = size;
    entries_.push_back(std::move(entry));
  }

  return true;
}

