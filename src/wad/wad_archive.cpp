#include "wad/wad_archive.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {
constexpr int kWadHeaderSize = 12;
constexpr int kQ12WadDirEntrySize = 32;
constexpr int kDoomWadDirEntrySize = 16;

[[nodiscard]] quint32 read_u32_le_from(const char* p) {
  const quint32 b0 = static_cast<quint8>(p[0]);
  const quint32 b1 = static_cast<quint8>(p[1]);
  const quint32 b2 = static_cast<quint8>(p[2]);
  const quint32 b3 = static_cast<quint8>(p[3]);
  return (b0) | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

[[nodiscard]] qint32 read_i32_le_from(const char* p) {
  return static_cast<qint32>(read_u32_le_from(p));
}

[[nodiscard]] bool is_miptex_lump_type(quint8 type) {
  // Common conventions:
  // - WAD2: 'D' (0x44) miptex
  // - WAD3: 'C' (0x43) miptex
  return type == static_cast<quint8>('C') || type == static_cast<quint8>('D');
}

[[nodiscard]] bool is_qpic_lump_type(quint8 type) {
  // Quake WAD2 convention: 'B' (0x42) qpic.
  return type == static_cast<quint8>('B');
}

[[nodiscard]] bool looks_like_qpic_lump(QFile& f, quint32 file_pos, quint32 size) {
  // QPIC: 32-bit LE width/height header + 8bpp indices.
  if (size < 8) {
    return false;
  }
  if (!f.seek(static_cast<qint64>(file_pos))) {
    return false;
  }
  const QByteArray header = f.read(8);
  if (header.size() != 8) {
    return false;
  }
  const quint32 w = read_u32_le_from(header.constData() + 0);
  const quint32 h = read_u32_le_from(header.constData() + 4);
  if (w == 0 || h == 0) {
    return false;
  }
  constexpr quint32 kMaxDim = 16384;
  if (w > kMaxDim || h > kMaxDim) {
    return false;
  }
  const quint64 want = 8ULL + static_cast<quint64>(w) * static_cast<quint64>(h);
  return want == static_cast<quint64>(size);
}
}  // namespace

QString WadArchive::normalize_entry_name(QString name) {
  name = name.trimmed();
  name.replace('\\', '/');
  return name.toLower();
}

QString WadArchive::clean_lump_base_name(const QByteArray& raw_name_bytes) {
  if (raw_name_bytes.isEmpty()) {
    return {};
  }
  const int nul = raw_name_bytes.indexOf('\0');
  const QByteArray raw = (nul >= 0) ? raw_name_bytes.left(nul) : raw_name_bytes;
  QString out = QString::fromLatin1(raw).trimmed();
  // WAD lump names should not contain path separators, but be defensive.
  out.replace('\\', '/');
  while (out.startsWith('/')) {
    out.remove(0, 1);
  }
  while (out.endsWith('/')) {
    out.chop(1);
  }
  return out;
}

const WadArchive::LumpMeta* WadArchive::find_lump(const QString& name) const {
  const QString key = normalize_entry_name(name);
  const int idx = index_by_name_.value(key, -1);
  if (idx < 0 || idx >= meta_by_index_.size()) {
    return nullptr;
  }
  return &meta_by_index_[idx];
}

bool WadArchive::load(const QString& path, QString* error) {
  if (error) {
    error->clear();
  }

  loaded_ = false;
  wad3_ = false;
  doom_wad_ = false;
  path_.clear();
  entries_.clear();
  meta_by_index_.clear();
  index_by_name_.clear();

  const QString abs = QFileInfo(path).absoluteFilePath();
  if (abs.isEmpty() || !QFileInfo::exists(abs)) {
    if (error) {
      *error = "WAD file not found.";
    }
    return false;
  }

  QFile f(abs);
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open WAD.";
    }
    return false;
  }

  const qint64 file_size = f.size();
  if (file_size < kWadHeaderSize) {
    if (error) {
      *error = "WAD header is incomplete.";
    }
    return false;
  }

  const QByteArray header = f.read(kWadHeaderSize);
  if (header.size() != kWadHeaderSize) {
    if (error) {
      *error = "WAD header is incomplete.";
    }
    return false;
  }

  const QByteArray magic = header.left(4);
  const bool is_q12_wad = (magic == "WAD2" || magic == "WAD3");
  const bool is_doom_wad = (magic == "IWAD" || magic == "PWAD");
  if (!is_q12_wad && !is_doom_wad) {
    if (error) {
      *error = "Not a supported WAD (expected WAD2/WAD3/IWAD/PWAD).";
    }
    return false;
  }
  wad3_ = (magic == "WAD3");
  doom_wad_ = is_doom_wad;

  const qint32 lump_count = read_i32_le_from(header.constData() + 4);
  const qint32 dir_offset = read_i32_le_from(header.constData() + 8);

  if (lump_count < 0 || lump_count > 200000) {
    if (error) {
      *error = "WAD directory count is invalid.";
    }
    return false;
  }
  const int dir_entry_size = is_doom_wad ? kDoomWadDirEntrySize : kQ12WadDirEntrySize;
  const qint64 dir_bytes = static_cast<qint64>(lump_count) * dir_entry_size;
  if (dir_offset < 0 || dir_bytes < 0 || dir_offset + dir_bytes > file_size) {
    if (error) {
      *error = "WAD directory offset is invalid.";
    }
    return false;
  }

  if (!f.seek(dir_offset)) {
    if (error) {
      *error = "Unable to seek WAD directory.";
    }
    return false;
  }

  entries_.reserve(lump_count);
  meta_by_index_.reserve(lump_count);
  index_by_name_.reserve(lump_count);

  for (int i = 0; i < lump_count; ++i) {
    const QByteArray entry_bytes = f.read(dir_entry_size);
    if (entry_bytes.size() != dir_entry_size) {
      if (error) {
        *error = "Unable to read WAD directory entry.";
      }
      return false;
    }
    const char* p = entry_bytes.constData();

    const quint32 file_pos = read_u32_le_from(p + 0);
    quint32 disk_size = 0;
    quint32 size = 0;
    quint8 type = 0;
    quint8 compression = 0;
    QByteArray name_bytes;

    if (is_doom_wad) {
      disk_size = read_u32_le_from(p + 4);
      size = disk_size;
      name_bytes.resize(8);
      memcpy(name_bytes.data(), p + 8, 8);
    } else {
      disk_size = read_u32_le_from(p + 4);
      size = read_u32_le_from(p + 8);
      type = static_cast<quint8>(p[12]);
      compression = static_cast<quint8>(p[13]);
      name_bytes.resize(16);
      memcpy(name_bytes.data(), p + 16, 16);
    }

    const QString base = clean_lump_base_name(name_bytes);
    if (base.isEmpty()) {
      continue;
    }

    if (!is_doom_wad) {
      if (disk_size != size) {
        // Compression isn't expected for WAD2/WAD3 in common use, but disk_size != size implies compression/packing.
        if (error) {
          *error = QString("WAD lump appears compressed/packed (disk_size=%1, size=%2): %3").arg(disk_size).arg(size).arg(base);
        }
        return false;
      }
      if (compression != 0) {
        if (error) {
          *error = QString("WAD lump compression is not supported (compression=%1): %2").arg(compression).arg(base);
        }
        return false;
      }
    }

    const qint64 end = static_cast<qint64>(file_pos) + static_cast<qint64>(disk_size);
    if (end < 0 || end > file_size) {
      if (error) {
        *error = QString("WAD lump is out of bounds: %1").arg(base);
      }
      return false;
    }

    QString entry_name = base;
    if (!is_doom_wad && !entry_name.contains('.')) {
      if (is_miptex_lump_type(type)) {
        entry_name += ".mip";
      } else if (normalize_entry_name(entry_name) == "palette") {
        // Common in some Quake/GoldSrc WAD texture packs (raw 256*RGB palette).
        entry_name += ".lmp";
      } else if (is_qpic_lump_type(type) || looks_like_qpic_lump(f, file_pos, size)) {
        // Quake WAD menu images and other pics (QPIC) are effectively .lmp images.
        entry_name += ".lmp";
      }
    }

    // Ensure uniqueness.
    QString unique = entry_name;
    int suffix = 2;
    while (index_by_name_.contains(normalize_entry_name(unique))) {
      unique = QString("%1_%2").arg(entry_name).arg(suffix++);
    }
    entry_name = unique;

    ArchiveEntry e;
    e.name = entry_name;
    e.offset = file_pos;
    e.size = size;
    e.mtime_utc_secs = -1;

    entries_.push_back(e);
    meta_by_index_.push_back(LumpMeta{file_pos, disk_size, type, compression});
    index_by_name_.insert(normalize_entry_name(entry_name), entries_.size() - 1);
  }

  loaded_ = true;
  path_ = abs;
  return true;
}

bool WadArchive::read_entry_bytes(const QString& name, QByteArray* out, QString* error, qint64 max_bytes) const {
  if (out) {
    out->clear();
  }
  if (error) {
    error->clear();
  }
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No WAD is loaded.";
    }
    return false;
  }

  const LumpMeta* meta = find_lump(name);
  if (!meta) {
    if (error) {
      *error = QString("Entry not found: %1").arg(name);
    }
    return false;
  }

  QFile file(path_);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = "Unable to open WAD for reading.";
    }
    return false;
  }

  qint64 to_read = static_cast<qint64>(meta->disk_size);
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

bool WadArchive::extract_entry_to_file(const QString& name, const QString& dest_path, QString* error) const {
  if (error) {
    error->clear();
  }
  if (!loaded_ || path_.isEmpty()) {
    if (error) {
      *error = "No WAD is loaded.";
    }
    return false;
  }

  const LumpMeta* meta = find_lump(name);
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
      *error = "Unable to open source WAD for reading.";
    }
    return false;
  }

  if (!src.seek(static_cast<qint64>(meta->offset))) {
    if (error) {
      *error = QString("Unable to seek source entry: %1").arg(name);
    }
    return false;
  }

  QSaveFile out(dest_path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = "Unable to create output file.";
    }
    return false;
  }

  constexpr qint64 kChunk = 1024 * 1024;
  qint64 remaining = static_cast<qint64>(meta->disk_size);
  QByteArray buf;
  buf.resize(static_cast<int>(qMin<qint64>(kChunk, remaining)));
  while (remaining > 0) {
    const qint64 want = qMin<qint64>(remaining, buf.size());
    const qint64 got = src.read(buf.data(), want);
    if (got != want) {
      if (error) {
        *error = QString("Unable to read entry: %1").arg(name);
      }
      return false;
    }
    if (out.write(buf.constData(), got) != got) {
      if (error) {
        *error = QString("Unable to write output file: %1").arg(dest_path);
      }
      return false;
    }
    remaining -= got;
  }

  if (!out.commit()) {
    if (error) {
      *error = "Unable to finalize output file.";
    }
    return false;
  }

  return true;
}
