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
constexpr int kQuakePakDirEntrySize = 64;
constexpr int kQuakePakNameBytes = 56;
constexpr int kSinDirEntrySize = 128;
constexpr int kSinNameBytes = 120;

struct PakLayout {
  QByteArray signature;
  int dir_entry_size = 0;
  int name_bytes = 0;
  bool sin_archive = false;
};

PakLayout quake_pak_layout() {
  PakLayout out;
  out.signature = "PACK";
  out.dir_entry_size = kQuakePakDirEntrySize;
  out.name_bytes = kQuakePakNameBytes;
  out.sin_archive = false;
  return out;
}

PakLayout sin_pak_layout() {
  PakLayout out;
  out.signature = "SPAK";
  out.dir_entry_size = kSinDirEntrySize;
  out.name_bytes = kSinNameBytes;
  out.sin_archive = true;
  return out;
}

bool pak_layout_from_signature(const QByteArray& signature, PakLayout* out) {
  if (!out) {
    return false;
  }
  if (signature == "PACK") {
    *out = quake_pak_layout();
    return true;
  }
  if (signature == "SPAK") {
    *out = sin_pak_layout();
    return true;
  }
  return false;
}

PakLayout pak_layout_for_output_path(const QString& dest_path, bool prefer_sin_variant) {
  const QString ext = QFileInfo(dest_path).suffix().toLower();
  if (ext == "sin") {
    return sin_pak_layout();
  }
  if (ext == "pak") {
    return quake_pak_layout();
  }
  if (prefer_sin_variant) {
    return sin_pak_layout();
  }
  return quake_pak_layout();
}

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

QString normalize_lookup_name(QString name) {
  name = name.trimmed();
  name.replace('\\', '/');
  while (name.startsWith('/')) {
    name.remove(0, 1);
  }
  name = QDir::cleanPath(name);
  name.replace('\\', '/');
  if (name == ".") {
    name.clear();
  }
  return name;
}
}  // namespace

bool PakArchive::load(const QString& path, QString* error) {
  loaded_ = false;
  sin_archive_ = false;
  dir_entry_size_ = kQuakePakDirEntrySize;
  name_bytes_ = kQuakePakNameBytes;
  signature_ = "PACK";
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
      *error = "Archive file is too small.";
    }
    return false;
  }

  PakLayout layout;
  if (!pak_layout_from_signature(header.left(4), &layout)) {
    if (error) {
      *error = "Not a valid Quake/SiN archive (missing PACK/SPAK header).";
    }
    return false;
  }
  sin_archive_ = layout.sin_archive;
  dir_entry_size_ = layout.dir_entry_size;
  name_bytes_ = layout.name_bytes;
  signature_ = layout.signature;
  const QString archive_label = sin_archive_ ? "SiN archive" : "PAK";

  quint32 dir_offset = 0;
  quint32 dir_length = 0;
  if (!read_u32_le(header, 4, &dir_offset) || !read_u32_le(header, 8, &dir_length)) {
    if (error) {
      *error = QString("Unable to read %1 header.").arg(archive_label);
    }
    return false;
  }

  if (dir_length % static_cast<quint32>(dir_entry_size_) != 0) {
    if (error) {
      *error = QString("%1 directory has an invalid size.").arg(archive_label);
    }
    return false;
  }

  if (static_cast<qint64>(dir_offset) + static_cast<qint64>(dir_length) > file_size_) {
    if (error) {
      *error = QString("%1 directory extends past end of file.").arg(archive_label);
    }
    return false;
  }

  if (!file.seek(dir_offset)) {
    if (error) {
      *error = QString("%1 directory offset is invalid.").arg(archive_label);
    }
    return false;
  }

  const int count = static_cast<int>(dir_length / static_cast<quint32>(dir_entry_size_));
  constexpr int kMaxEntries = 1'000'000;
  if (count > kMaxEntries) {
    if (error) {
      *error = QString("%1 directory is too large.").arg(archive_label);
    }
    return false;
  }

  entries_.reserve(count);

  for (int i = 0; i < count; ++i) {
    const QByteArray entry_bytes = file.read(dir_entry_size_);
    if (entry_bytes.size() != dir_entry_size_) {
      if (error) {
        *error = QString("Unable to read %1 directory.").arg(archive_label);
      }
      return false;
    }

    const QByteArray raw_name = entry_bytes.left(name_bytes_);
    quint32 offset = 0;
    quint32 size = 0;
    if (!read_u32_le(entry_bytes, name_bytes_, &offset) ||
        !read_u32_le(entry_bytes, name_bytes_ + 4, &size)) {
      if (error) {
        *error = QString("Unable to read %1 directory entry.").arg(archive_label);
      }
      return false;
    }

    const QString name = sanitize_entry_name(raw_name);
    if (!is_safe_entry_name(name)) {
      if (error) {
        *error = QString("%1 contains an unsafe entry name: %2").arg(archive_label, name);
      }
      return false;
    }

    const qint64 end = static_cast<qint64>(offset) + static_cast<qint64>(size);
    if (end < 0 || end > file_size_) {
      if (error) {
        *error = QString("%1 entry extends past end of file: %2").arg(archive_label, name);
      }
      return false;
    }

    ArchiveEntry entry;
    entry.name = name;
    entry.offset = offset;
    entry.size = size;
    entry.mtime_utc_secs = -1;
    entries_.push_back(std::move(entry));
  }

  loaded_ = true;
  path_ = QFileInfo(path).absoluteFilePath();
  return true;
}

const ArchiveEntry* PakArchive::find_entry(const QString& name) const {
  if (!loaded_) {
    return nullptr;
  }
  const QString needle = normalize_lookup_name(name);
  if (needle.isEmpty()) {
    return nullptr;
  }
  for (const ArchiveEntry& e : entries_) {
    if (e.name == needle) {
      return &e;
    }
  }
  return nullptr;
}

bool PakArchive::read_entry_bytes(const QString& name,
                                 QByteArray* out,
                                 QString* error,
                                 qint64 max_bytes) const {
  if (out) {
    out->clear();
  }
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No archive is loaded.";
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

  QFile file(path_);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open archive for reading.";
    }
    return false;
  }

  const qint64 end = static_cast<qint64>(entry->offset) + static_cast<qint64>(entry->size);
  const qint64 file_size = file.size();
  if (end < 0 || end > file_size) {
    if (error) {
      *error = QString("Archive entry is out of bounds: %1").arg(entry->name);
    }
    return false;
  }

  qint64 to_read = entry->size;
  if (max_bytes >= 0 && to_read > max_bytes) {
    to_read = max_bytes;
  }

  if (!file.seek(static_cast<qint64>(entry->offset))) {
    if (error) {
      *error = QString("Unable to seek entry: %1").arg(entry->name);
    }
    return false;
  }

  QByteArray bytes = file.read(to_read);
  if (bytes.size() != to_read) {
    if (error) {
      *error = QString("Unable to read entry: %1").arg(entry->name);
    }
    return false;
  }

  if (out) {
    *out = std::move(bytes);
  }
  return true;
}

bool PakArchive::extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const {
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No archive is loaded.";
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

  QFile src(path_);
  if (!src.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open source archive for reading.";
    }
    return false;
  }

  const qint64 src_size = src.size();
  const qint64 end = static_cast<qint64>(entry->offset) + static_cast<qint64>(entry->size);
  if (end < 0 || end > src_size) {
    if (error) {
      *error = QString("Archive entry is out of bounds: %1").arg(entry->name);
    }
    return false;
  }

  if (!src.seek(static_cast<qint64>(entry->offset))) {
    if (error) {
      *error = QString("Unable to seek source entry: %1").arg(entry->name);
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

  constexpr qint64 kChunk = 1 << 16;
  QByteArray buffer;
  buffer.resize(static_cast<int>(kChunk));

  quint32 remaining = entry->size;
  while (remaining > 0) {
    const int want = static_cast<int>(std::min<quint32>(remaining, static_cast<quint32>(buffer.size())));
    const qint64 got = src.read(buffer.data(), want);
    if (got <= 0) {
      if (error) {
        *error = QString("Unable to read source entry: %1").arg(entry->name);
      }
      return false;
    }
    if (out.write(buffer.constData(), got) != got) {
      if (error) {
        *error = QString("Unable to write output file: %1").arg(dest_path);
      }
      return false;
    }
    remaining -= static_cast<quint32>(got);
  }

  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize output file: %1").arg(dest_path);
    }
    return false;
  }

  return true;
}

bool PakArchive::write_empty(const QString& dest_path, QString* error) {
  const PakLayout layout = pak_layout_for_output_path(dest_path, false);
  const QString archive_label = layout.sin_archive ? "SiN archive" : "PAK";

  QSaveFile out(dest_path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QString("Unable to create %1 file.").arg(archive_label);
    }
    return false;
  }

  QByteArray header(kPakHeaderSize, '\0');
  std::memcpy(header.data(), layout.signature.constData(), 4);
  write_u32_le(&header, 4, static_cast<quint32>(kPakHeaderSize));
  write_u32_le(&header, 8, 0);

  if (out.write(header) != header.size()) {
    if (error) {
      *error = QString("Unable to write %1 header.").arg(archive_label);
    }
    return false;
  }

  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize %1 file.").arg(archive_label);
    }
    return false;
  }

  return true;
}

bool PakArchive::save_as(const QString& dest_path, QString* error) const {
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No archive is loaded.";
    }
    return false;
  }

  const PakLayout layout = pak_layout_for_output_path(dest_path, sin_archive_);
  const QString archive_label = layout.sin_archive ? "SiN archive" : "PAK";
  const int dir_entry_size = layout.dir_entry_size;
  const int max_name_bytes = layout.name_bytes;

  QFile src(path_);
  if (!src.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = QString("Unable to open source %1 for reading.").arg(archive_label);
    }
    return false;
  }

  const qint64 src_size = src.size();

  QSaveFile out(dest_path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QString("Unable to create destination %1.").arg(archive_label);
    }
    return false;
  }

  QByteArray header(kPakHeaderSize, '\0');
  std::memcpy(header.data(), layout.signature.constData(), 4);
  // Offsets will be patched later.
  if (out.write(header) != header.size()) {
    if (error) {
      *error = QString("Unable to write %1 header.").arg(archive_label);
    }
    return false;
  }

  QVector<ArchiveEntry> new_entries;
  new_entries.reserve(entries_.size());

  constexpr qint64 kChunk = 1 << 16;
  QByteArray buffer;
  buffer.resize(static_cast<int>(kChunk));

  for (const ArchiveEntry& e : entries_) {
    if (!is_safe_entry_name(e.name)) {
      if (error) {
        *error = QString("Refusing to save unsafe entry: %1").arg(e.name);
      }
      return false;
    }

    const QByteArray entry_name_bytes = e.name.toLatin1();
    if (entry_name_bytes.size() > max_name_bytes) {
      if (error) {
        *error = QString("%1 entry name is too long: %2").arg(archive_label, e.name);
      }
      return false;
    }

    const qint64 end = static_cast<qint64>(e.offset) + static_cast<qint64>(e.size);
    if (end < 0 || end > src_size) {
      if (error) {
        *error = QString("%1 entry is out of bounds: %2").arg(archive_label, e.name);
      }
      return false;
    }

    const qint64 out_offset64 = out.pos();
    if (out_offset64 < 0 || out_offset64 > std::numeric_limits<quint32>::max()) {
      if (error) {
        *error = QString("%1 output exceeds format limits.").arg(archive_label);
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

    ArchiveEntry out_entry;
    out_entry.name = e.name;
    out_entry.offset = out_offset;
    out_entry.size = e.size;
    out_entry.mtime_utc_secs = -1;
    new_entries.push_back(out_entry);
  }

  const qint64 dir_offset64 = out.pos();
  if (dir_offset64 < 0 || dir_offset64 > std::numeric_limits<quint32>::max()) {
    if (error) {
      *error = QString("%1 output exceeds format limits.").arg(archive_label);
    }
    return false;
  }
  const quint32 dir_offset = static_cast<quint32>(dir_offset64);

  const qint64 dir_length64 = static_cast<qint64>(new_entries.size()) * dir_entry_size;
  if (dir_length64 < 0 || dir_length64 > std::numeric_limits<quint32>::max()) {
    if (error) {
      *error = QString("%1 directory exceeds format limits.").arg(archive_label);
    }
    return false;
  }
  const quint32 dir_length = static_cast<quint32>(dir_length64);

  QByteArray dir;
  dir.resize(static_cast<int>(dir_length));
  dir.fill('\0');

  for (int i = 0; i < new_entries.size(); ++i) {
    const ArchiveEntry& e = new_entries[i];
    const int base = i * dir_entry_size;
    const QByteArray name = e.name.toLatin1();
    std::memcpy(dir.data() + base, name.constData(), static_cast<size_t>(name.size()));
    write_u32_le(&dir, base + max_name_bytes, e.offset);
    write_u32_le(&dir, base + max_name_bytes + 4, e.size);
  }

  if (out.write(dir) != dir.size()) {
    if (error) {
      *error = QString("Unable to write %1 directory.").arg(archive_label);
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
      *error = QString("Unable to update %1 header.").arg(archive_label);
    }
    return false;
  }

  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize destination %1.").arg(archive_label);
    }
    return false;
  }

  return true;
}
