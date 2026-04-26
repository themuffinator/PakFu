#include "wad/wad_archive.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "archive/path_safety.h"

namespace {
constexpr int kWadHeaderSize = 12;
constexpr int kQ12WadDirEntrySize = 32;
constexpr int kDoomWadDirEntrySize = 16;
constexpr int kWadNameBytes = 16;
constexpr quint8 kWadTypeNone = 0;
constexpr quint8 kWadTypeQpic = static_cast<quint8>('B');
constexpr quint8 kWadTypeMiptexWad2 = static_cast<quint8>('D');
constexpr quint8 kWadTypeLumpy = 64;

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

[[nodiscard]] bool looks_like_qpic_lump_bytes(const QByteArray& bytes) {
  if (bytes.size() < 8) {
    return false;
  }
  const quint32 w = read_u32_le_from(bytes.constData() + 0);
  const quint32 h = read_u32_le_from(bytes.constData() + 4);
  if (w == 0 || h == 0) {
    return false;
  }
  constexpr quint32 kMaxDim = 16384;
  if (w > kMaxDim || h > kMaxDim) {
    return false;
  }
  const quint64 want = 8ULL + static_cast<quint64>(w) * static_cast<quint64>(h);
  return want == static_cast<quint64>(bytes.size());
}

void write_u32_le_to(QByteArray* bytes, int offset, quint32 value) {
  if (!bytes || offset < 0 || offset + 4 > bytes->size()) {
    return;
  }
  (*bytes)[offset + 0] = static_cast<char>(value & 0xFF);
  (*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
  (*bytes)[offset + 2] = static_cast<char>((value >> 16) & 0xFF);
  (*bytes)[offset + 3] = static_cast<char>((value >> 24) & 0xFF);
}

[[nodiscard]] quint8 derive_wad2_lump_type(const QString& entry_name_in,
                                           const QString& lump_name,
                                           const QByteArray* bytes = nullptr) {
  const QString lower = normalize_archive_entry_name(entry_name_in).toLower();
  if (lower.endsWith(".mip")) {
    return kWadTypeMiptexWad2;
  }
  if (lower.endsWith(".lmp")) {
    if (lump_name.compare("palette", Qt::CaseInsensitive) == 0) {
      return kWadTypeLumpy;
    }
    if (bytes && looks_like_qpic_lump_bytes(*bytes)) {
      return kWadTypeQpic;
    }
    return kWadTypeQpic;
  }
  if (bytes && looks_like_qpic_lump_bytes(*bytes)) {
    return kWadTypeQpic;
  }
  return kWadTypeNone;
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

bool WadArchive::derive_wad2_lump_name(const QString& entry_name_in, QString* out_lump_name, QString* error) {
  if (error) {
    error->clear();
  }

  const QString entry_name = normalize_archive_entry_name(entry_name_in);
  if (entry_name.isEmpty()) {
    if (error) {
      *error = "WAD entry name is empty.";
    }
    return false;
  }
  if (entry_name.contains('/')) {
    if (error) {
      *error = QString("WAD entries cannot contain folders: %1").arg(entry_name);
    }
    return false;
  }

  QString lump_name = entry_name;
  const int dot = lump_name.lastIndexOf('.');
  if (dot > 0) {
    const QString ext = lump_name.mid(dot + 1).toLower();
    if (ext == "mip" || ext == "lmp") {
      lump_name = lump_name.left(dot);
    }
  }

  if (lump_name.isEmpty()) {
    if (error) {
      *error = QString("WAD entry has an invalid lump name: %1").arg(entry_name);
    }
    return false;
  }

  const QByteArray lump_latin1 = lump_name.toLatin1();
  if (QString::fromLatin1(lump_latin1) != lump_name) {
    if (error) {
      *error = QString("WAD entry name must be Latin-1: %1").arg(entry_name);
    }
    return false;
  }
  if (lump_latin1.size() > kWadNameBytes) {
    if (error) {
      *error = QString("WAD lump names are limited to %1 bytes: %2").arg(kWadNameBytes).arg(lump_name);
    }
    return false;
  }

  if (out_lump_name) {
    *out_lump_name = lump_name;
  }
  return true;
}

bool WadArchive::write_wad2(const QString& dest_path, const WritePlan& plan, QString* error) {
  if (error) {
    error->clear();
  }

  const QString abs = QFileInfo(dest_path).absoluteFilePath();
  if (abs.isEmpty()) {
    if (error) {
      *error = "Invalid destination path.";
    }
    return false;
  }

  WadArchive source_wad;
  const bool needs_source_wad = std::any_of(plan.entries.cbegin(), plan.entries.cend(), [](const WriteEntry& e) {
    return e.from_source_wad;
  });
  if (needs_source_wad) {
    if (plan.source_wad_path.isEmpty()) {
      if (error) {
        *error = "No source WAD path was provided.";
      }
      return false;
    }
    QString load_err;
    if (!source_wad.load(plan.source_wad_path, &load_err)) {
      if (error) {
        *error = load_err.isEmpty() ? "Unable to load source WAD." : load_err;
      }
      return false;
    }
  }

  struct WadWriteItem {
    QString entry_name;
    QString source_path;
    bool from_source_wad = false;
    QString lump_name;
  };

  QVector<WadWriteItem> items;
  items.reserve(plan.entries.size());

  QHash<QString, QString> lump_owner_by_key;
  lump_owner_by_key.reserve(plan.entries.size());

  for (const WriteEntry& entry : plan.entries) {
    const QString entry_name = normalize_archive_entry_name(entry.entry_name);
    if (entry_name.isEmpty()) {
      continue;
    }
    if (!is_safe_archive_entry_name(entry_name)) {
      if (error) {
        *error = QString("Refusing to save unsafe entry: %1").arg(entry_name);
      }
      return false;
    }

    QString lump_name;
    QString lump_err;
    if (!derive_wad2_lump_name(entry_name, &lump_name, &lump_err)) {
      if (error) {
        *error = lump_err.isEmpty() ? QString("Invalid WAD entry name: %1").arg(entry_name) : lump_err;
      }
      return false;
    }

    const QString key = lump_name.toLower();
    const QString existing = lump_owner_by_key.value(key);
    if (!existing.isEmpty()) {
      if (error) {
        *error = QString("Duplicate WAD lump name after normalization: %1 (from %2 and %3)")
                   .arg(lump_name, existing, entry_name);
      }
      return false;
    }
    lump_owner_by_key.insert(key, entry_name);

    WadWriteItem item;
    item.entry_name = entry_name;
    item.source_path = entry.source_path;
    item.from_source_wad = entry.from_source_wad;
    item.lump_name = lump_name;
    items.push_back(std::move(item));
  }

  QSaveFile out(abs);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = "Unable to create destination WAD.";
    }
    return false;
  }

  QByteArray header(kWadHeaderSize, '\0');
  std::memcpy(header.data(), "WAD2", 4);
  if (out.write(header) != header.size()) {
    if (error) {
      *error = "Unable to write WAD2 header.";
    }
    return false;
  }

  struct WadDirEntry {
    quint32 file_pos = 0;
    quint32 disk_size = 0;
    quint32 size = 0;
    quint8 type = 0;
    QByteArray lump_name_latin1;
  };

  QVector<WadDirEntry> dir_entries;
  dir_entries.reserve(items.size());

  QByteArray buffer;
  buffer.resize(1 << 16);

  for (const WadWriteItem& item : items) {
    const qint64 out_pos = out.pos();
    if (out_pos < 0 || out_pos > std::numeric_limits<quint32>::max()) {
      if (error) {
        *error = "WAD2 output exceeds format limits.";
      }
      return false;
    }

    quint32 size = 0;
    quint8 type = kWadTypeNone;

    if (item.from_source_wad) {
      QByteArray bytes;
      QString read_err;
      if (!source_wad.read_entry_bytes(item.entry_name, &bytes, &read_err)) {
        if (error) {
          *error = read_err.isEmpty() ? QString("Unable to read source entry: %1").arg(item.entry_name) : read_err;
        }
        return false;
      }
      if (static_cast<quint64>(bytes.size()) > std::numeric_limits<quint32>::max()) {
        if (error) {
          *error = QString("Entry is too large for WAD2 format: %1").arg(item.entry_name);
        }
        return false;
      }
      if (out.write(bytes) != bytes.size()) {
        if (error) {
          *error = QString("Unable to write destination entry: %1").arg(item.entry_name);
        }
        return false;
      }
      size = static_cast<quint32>(bytes.size());
      type = derive_wad2_lump_type(item.entry_name, item.lump_name, &bytes);
    } else {
      QFile in(item.source_path);
      if (!in.open(QIODevice::ReadOnly)) {
        if (error) {
          *error = QString("Unable to open file: %1").arg(item.source_path);
        }
        return false;
      }

      const qint64 in_size64 = in.size();
      if (in_size64 < 0 || in_size64 > std::numeric_limits<quint32>::max()) {
        if (error) {
          *error = QString("File is too large for WAD2 format: %1").arg(item.source_path);
        }
        return false;
      }
      size = static_cast<quint32>(in_size64);
      type = derive_wad2_lump_type(item.entry_name, item.lump_name, nullptr);

      quint32 remaining = size;
      while (remaining > 0) {
        const int want =
          static_cast<int>(std::min<quint32>(remaining, static_cast<quint32>(buffer.size())));
        const qint64 got = in.read(buffer.data(), want);
        if (got <= 0) {
          if (error) {
            *error = QString("Unable to read file: %1").arg(item.source_path);
          }
          return false;
        }
        if (out.write(buffer.constData(), got) != got) {
          if (error) {
            *error = QString("Unable to write destination entry: %1").arg(item.entry_name);
          }
          return false;
        }
        remaining -= static_cast<quint32>(got);
      }
    }

    WadDirEntry d;
    d.file_pos = static_cast<quint32>(out_pos);
    d.disk_size = size;
    d.size = size;
    d.type = type;
    d.lump_name_latin1 = item.lump_name.toLatin1();
    dir_entries.push_back(std::move(d));
  }

  const qint64 dir_offset64 = out.pos();
  if (dir_offset64 < 0 || dir_offset64 > std::numeric_limits<quint32>::max()) {
    if (error) {
      *error = "WAD2 output exceeds format limits.";
    }
    return false;
  }
  const quint32 dir_offset = static_cast<quint32>(dir_offset64);

  const qint64 dir_bytes64 = static_cast<qint64>(dir_entries.size()) * kQ12WadDirEntrySize;
  if (dir_bytes64 < 0 || dir_bytes64 > std::numeric_limits<int>::max()) {
    if (error) {
      *error = "WAD2 directory exceeds format limits.";
    }
    return false;
  }

  QByteArray dir;
  dir.resize(static_cast<int>(dir_bytes64));
  dir.fill('\0');
  for (int i = 0; i < dir_entries.size(); ++i) {
    const WadDirEntry& d = dir_entries[i];
    const int base = i * kQ12WadDirEntrySize;
    write_u32_le_to(&dir, base + 0, d.file_pos);
    write_u32_le_to(&dir, base + 4, d.disk_size);
    write_u32_le_to(&dir, base + 8, d.size);
    dir[base + 12] = static_cast<char>(d.type);
    dir[base + 13] = 0;
    dir[base + 14] = 0;
    dir[base + 15] = 0;
    const QByteArray lump = d.lump_name_latin1.left(kWadNameBytes);
    if (!lump.isEmpty()) {
      std::memcpy(dir.data() + base + 16, lump.constData(), static_cast<size_t>(lump.size()));
    }
  }

  if (out.write(dir) != dir.size()) {
    if (error) {
      *error = "Unable to write WAD2 directory.";
    }
    return false;
  }

  write_u32_le_to(&header, 4, static_cast<quint32>(dir_entries.size()));
  write_u32_le_to(&header, 8, dir_offset);
  if (!out.seek(0) || out.write(header) != header.size()) {
    if (error) {
      *error = "Unable to update WAD2 header.";
    }
    return false;
  }

  if (!out.commit()) {
    if (error) {
      *error = "Unable to finalize destination WAD2.";
    }
    return false;
  }

  return true;
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
