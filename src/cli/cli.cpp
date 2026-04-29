#include "cli.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>

#include "archive/archive.h"
#include "archive/archive_search_index.h"
#include "archive/path_safety.h"
#include "extensions/extension_plugin.h"
#include "formats/fontdat_font.h"
#include "formats/idtech4_map.h"
#include "formats/idwav_audio.h"
#include "formats/image_loader.h"
#include "formats/image_writer.h"
#include "game/game_auto_detect.h"
#include "game/game_set.h"
#include "pakfu_config.h"
#include "ui/practical_qa.h"
#include "update/update_service.h"
#include "wad/wad_archive.h"
#include "zip/zip_archive.h"

namespace {
QString normalize_output(const QString& text) {
  return text.endsWith('\n') ? text : text + '\n';
}

constexpr int kPakHeaderSize = 12;
constexpr int kPakDirEntrySize = 64;
constexpr int kPakNameBytes = 56;
constexpr int kSinDirEntrySize = 128;
constexpr int kSinNameBytes = 120;

struct CliDiskFile {
  QString archive_name;
  QString source_path;
  qint64 mtime_utc_secs = -1;
};

enum class SaveArchiveFormat {
  Pak,
  Sin,
  Zip,
  Wad2,
};

void write_u32_le(QByteArray* bytes, int offset, quint32 value) {
  if (!bytes || offset < 0 || offset + 4 > bytes->size()) {
    return;
  }
  (*bytes)[offset + 0] = static_cast<char>(value & 0xff);
  (*bytes)[offset + 1] = static_cast<char>((value >> 8) & 0xff);
  (*bytes)[offset + 2] = static_cast<char>((value >> 16) & 0xff);
  (*bytes)[offset + 3] = static_cast<char>((value >> 24) & 0xff);
}

QString file_ext_lower(const QString& name) {
  const QString lower = name.toLower();
  const int dot = lower.lastIndexOf('.');
  return dot >= 0 ? lower.mid(dot + 1) : QString();
}

QString format_string(Archive::Format f) {
  switch (f) {
    case Archive::Format::Directory:
      return "DIR";
    case Archive::Format::Pak:
      return "PAK";
    case Archive::Format::Wad:
      return "WAD";
    case Archive::Format::Resources:
      return "RESOURCES";
    case Archive::Format::Zip:
      return "ZIP";
    case Archive::Format::Unknown:
      break;
  }
  return "Unknown";
}

QString archive_format_label(const Archive& archive) {
  switch (archive.format()) {
    case Archive::Format::Directory:
      return "directory";
    case Archive::Format::Pak:
      return "pak";
    case Archive::Format::Wad:
      return "wad";
    case Archive::Format::Resources:
      return "resources";
    case Archive::Format::Zip:
      return "zip";
    case Archive::Format::Unknown:
      break;
  }
  return "unknown";
}

QString change_file_extension(QString path, const QString& format) {
  QString ext = format.trimmed().toLower();
  if (ext.startsWith('.')) {
    ext.remove(0, 1);
  }
  if (ext.isEmpty()) {
    return path;
  }
  const int slash = path.lastIndexOf('/');
  const int dot = path.lastIndexOf('.');
  if (dot > slash) {
    path.truncate(dot);
  }
  path += '.';
  path += ext;
  return path;
}

QString entry_to_local_path(const QString& root, const QString& entry_name) {
  return QDir(root).filePath(QString(entry_name).replace('/', QDir::separator()));
}

bool ensure_parent_dir(const QString& file_path, QString* error) {
  const QFileInfo info(file_path);
  QDir parent(info.absolutePath());
  if (parent.exists() || parent.mkpath(".")) {
    return true;
  }
  if (error) {
    *error = QString("Unable to create output directory: %1").arg(parent.absolutePath());
  }
  return false;
}

bool write_bytes_file(const QString& file_path, const QByteArray& bytes, QString* error) {
  if (!ensure_parent_dir(file_path, error)) {
    return false;
  }
  QSaveFile out(file_path);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QString("Unable to create output file: %1").arg(file_path);
    }
    return false;
  }
  if (out.write(bytes) != bytes.size()) {
    if (error) {
      *error = QString("Unable to write output file: %1").arg(file_path);
    }
    return false;
  }
  if (!out.commit()) {
    if (error) {
      *error = QString("Unable to finalize output file: %1").arg(file_path);
    }
    return false;
  }
  return true;
}

bool looks_like_text(const QByteArray& bytes) {
  if (bytes.isEmpty()) {
    return true;
  }
  int printable = 0;
  int control = 0;
  for (const char c : bytes) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u == 0) {
      return false;
    }
    if (u == '\n' || u == '\r' || u == '\t') {
      ++printable;
      continue;
    }
    if (u >= 32 && u < 127) {
      ++printable;
      continue;
    }
    if (u < 32) {
      ++control;
    }
  }
  const int total = bytes.size();
  return total <= 0 || ((printable * 100) / total >= 85 && (control * 100) / total < 5);
}

bool is_decodable_image_ext(const QString& ext) {
  static const QSet<QString> kImageExts = {
    "dds", "ftx", "jpg", "jpeg", "lmp", "m8", "m32", "mip", "pcx", "png", "swl", "tga", "wal",
  };
  return kImageExts.contains(ext.toLower());
}

bool normalize_selection_filters(const CliOptions& options,
                                 QStringList* entry_filters,
                                 QStringList* prefix_filters,
                                 QString* error) {
  if (entry_filters) {
    entry_filters->clear();
  }
  if (prefix_filters) {
    prefix_filters->clear();
  }

  for (const QString& raw : options.entry_filters) {
    const QString name = normalize_archive_entry_name(raw);
    if (!is_safe_archive_entry_name(name)) {
      if (error) {
        *error = QString("Unsafe --entry value: %1").arg(raw);
      }
      return false;
    }
    if (entry_filters) {
      entry_filters->push_back(name);
    }
  }

  for (const QString& raw : options.prefix_filters) {
    QString prefix = normalize_archive_entry_name(raw);
    while (prefix.endsWith('/')) {
      prefix.chop(1);
    }
    if (!is_safe_archive_entry_name(prefix)) {
      if (error) {
        *error = QString("Unsafe --prefix value: %1").arg(raw);
      }
      return false;
    }
    if (prefix_filters) {
      prefix_filters->push_back(prefix);
    }
  }

  if (error) {
    error->clear();
  }
  return true;
}

bool has_selection_filters(const QStringList& entry_filters, const QStringList& prefix_filters) {
  return !entry_filters.isEmpty() || !prefix_filters.isEmpty();
}

QStringList extension_search_dirs_for_options(const CliOptions& options) {
  QStringList out;
  QSet<QString> seen;
  const auto append_dir = [&](const QString& path) {
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute.isEmpty()) {
      return;
    }
    const QString key = QDir::cleanPath(absolute).toLower();
    if (seen.contains(key)) {
      return;
    }
    seen.insert(key);
    out.push_back(QDir::cleanPath(absolute));
  };

  for (const QString& path : options.plugin_dirs) {
    append_dir(path);
  }
  for (const QString& path : default_extension_search_dirs()) {
    append_dir(path);
  }
  return out;
}

bool entry_matches_filters(const QString& normalized_name,
                           const QStringList& entry_filters,
                           const QStringList& prefix_filters) {
  if (!has_selection_filters(entry_filters, prefix_filters)) {
    return true;
  }
  for (const QString& entry : entry_filters) {
    if (normalized_name.compare(entry, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  QString without_trailing = normalized_name;
  while (without_trailing.endsWith('/')) {
    without_trailing.chop(1);
  }
  for (const QString& prefix : prefix_filters) {
    if (without_trailing.compare(prefix, Qt::CaseInsensitive) == 0 ||
        normalized_name.startsWith(prefix + '/', Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

QVector<const ArchiveEntry*> select_entries(const QVector<ArchiveEntry>& entries,
                                            const QStringList& entry_filters,
                                            const QStringList& prefix_filters,
                                            bool include_directories) {
  QVector<const ArchiveEntry*> selected;
  selected.reserve(entries.size());
  for (const ArchiveEntry& e : entries) {
    const QString name = normalize_archive_entry_name(e.name);
    if (name.isEmpty()) {
      continue;
    }
    if (name.endsWith('/') && !include_directories) {
      continue;
    }
    if (entry_matches_filters(name, entry_filters, prefix_filters)) {
      selected.push_back(&e);
    }
  }
  return selected;
}

bool resolve_save_archive_format(const CliOptions& options, SaveArchiveFormat* out, QString* error) {
  QString fmt = options.save_format.trimmed().toLower();
  if (fmt.startsWith('.')) {
    fmt.remove(0, 1);
  }
  if (fmt.isEmpty()) {
    fmt = file_ext_lower(options.save_as_path);
  }
  if (fmt == "pak") {
    *out = SaveArchiveFormat::Pak;
    return true;
  }
  if (fmt == "sin") {
    *out = SaveArchiveFormat::Sin;
    return true;
  }
  if (fmt == "zip" || fmt == "pk3" || fmt == "pk4" || fmt == "pkz") {
    *out = SaveArchiveFormat::Zip;
    return true;
  }
  if (fmt == "wad" || fmt == "wad2") {
    *out = SaveArchiveFormat::Wad2;
    return true;
  }
  if (error) {
    *error = "Unsupported --save-as format. Use pak, sin, zip, pk3, pk4, pkz, wad, or wad2.";
  }
  return false;
}

bool collect_disk_files(const Archive& archive,
                        const QVector<const ArchiveEntry*>& selected,
                        QTemporaryDir* temp,
                        QVector<CliDiskFile>* out_files,
                        QString* error) {
  if (out_files) {
    out_files->clear();
  }
  if (archive.format() != Archive::Format::Directory && (!temp || !temp->isValid())) {
    if (error) {
      *error = "Unable to create temporary extraction directory.";
    }
    return false;
  }

  for (const ArchiveEntry* entry : selected) {
    if (!entry) {
      continue;
    }
    const QString name = normalize_archive_entry_name(entry->name);
    if (name.isEmpty() || name.endsWith('/')) {
      continue;
    }
    if (!is_safe_archive_entry_name(name)) {
      if (error) {
        *error = QString("Refusing unsafe entry: %1").arg(entry->name);
      }
      return false;
    }

    QString source_path;
    if (archive.format() == Archive::Format::Directory) {
      source_path = entry_to_local_path(archive.path(), name);
      if (!QFileInfo(source_path).isFile()) {
        if (error) {
          *error = QString("Source file not found: %1").arg(source_path);
        }
        return false;
      }
    } else {
      source_path = entry_to_local_path(temp->path(), name);
      if (!ensure_parent_dir(source_path, error)) {
        return false;
      }
      QString extract_err;
      if (!archive.extract_entry_to_file(name, source_path, &extract_err)) {
        if (error) {
          *error = extract_err.isEmpty() ? QString("Unable to materialize entry: %1").arg(name) : extract_err;
        }
        return false;
      }
    }

    CliDiskFile f;
    f.archive_name = name;
    f.source_path = source_path;
    f.mtime_utc_secs = entry->mtime_utc_secs;
    if (out_files) {
      out_files->push_back(std::move(f));
    }
  }
  if (error) {
    error->clear();
  }
  return true;
}

bool collect_extension_entries(const Archive& archive,
                               const QStringList& entry_filters,
                               const QStringList& prefix_filters,
                               QTemporaryDir* temp,
                               QVector<ExtensionEntryContext>* out_entries,
                               QString* error) {
  if (error) {
    error->clear();
  }
  if (out_entries) {
    out_entries->clear();
  }
  if (!has_selection_filters(entry_filters, prefix_filters)) {
    return true;
  }

  const QVector<const ArchiveEntry*> selected = select_entries(archive.entries(), entry_filters, prefix_filters, false);
  if (selected.isEmpty()) {
    if (error) {
      *error = "No file entries matched the extension selection.";
    }
    return false;
  }

  QVector<CliDiskFile> files;
  QString collect_err;
  if (!collect_disk_files(archive, selected, temp, &files, &collect_err)) {
    if (error) {
      *error = collect_err;
    }
    return false;
  }

  if (out_entries) {
    out_entries->reserve(files.size());
    for (int i = 0; i < files.size(); ++i) {
      ExtensionEntryContext entry;
      entry.archive_name = files[i].archive_name;
      entry.local_path = files[i].source_path;
      entry.is_dir = false;
      if (i < selected.size() && selected[i]) {
        entry.size = selected[i]->size;
        entry.mtime_utc_secs = selected[i]->mtime_utc_secs;
      } else {
        entry.mtime_utc_secs = files[i].mtime_utc_secs;
      }
      out_entries->push_back(std::move(entry));
    }
  }

  return true;
}

bool write_pak_from_disk_files(const QString& dest_path,
                               const QVector<CliDiskFile>& files,
                               bool write_sin,
                               QString* error) {
  const QString abs = QFileInfo(dest_path).absoluteFilePath();
  if (abs.isEmpty()) {
    if (error) {
      *error = "Invalid destination path.";
    }
    return false;
  }
  if (!ensure_parent_dir(abs, error)) {
    return false;
  }

  const int name_bytes_limit = write_sin ? kSinNameBytes : kPakNameBytes;
  const int dir_entry_size = write_sin ? kSinDirEntrySize : kPakDirEntrySize;
  const QByteArray signature = write_sin ? QByteArray("SPAK", 4) : QByteArray("PACK", 4);
  const QString archive_label = write_sin ? "SiN archive" : "PAK";

  QSaveFile out(abs);
  if (!out.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QString("Unable to create destination %1.").arg(archive_label);
    }
    return false;
  }

  QByteArray header(kPakHeaderSize, '\0');
  std::memcpy(header.data(), signature.constData(), 4);
  if (out.write(header) != header.size()) {
    if (error) {
      *error = QString("Unable to write %1 header.").arg(archive_label);
    }
    return false;
  }

  QVector<ArchiveEntry> dir_entries;
  dir_entries.reserve(files.size());
  QByteArray buffer;
  buffer.resize(1 << 16);

  auto ensure_u32_pos = [&](qint64 pos, const QString& message) -> bool {
    if (pos < 0 || pos > std::numeric_limits<quint32>::max()) {
      if (error) {
        *error = message;
      }
      return false;
    }
    return true;
  };

  for (const CliDiskFile& f : files) {
    const QString name = normalize_archive_entry_name(f.archive_name);
    if (name.isEmpty() || name.endsWith('/') || !is_safe_archive_entry_name(name)) {
      if (error) {
        *error = QString("Refusing unsafe entry: %1").arg(f.archive_name);
      }
      return false;
    }
    const QByteArray name_bytes = name.toLatin1();
    if (name_bytes.isEmpty() || name_bytes.size() > name_bytes_limit) {
      if (error) {
        *error = QString("%1 entry name is too long: %2").arg(archive_label, name);
      }
      return false;
    }

    QFile in(f.source_path);
    if (!in.open(QIODevice::ReadOnly)) {
      if (error) {
        *error = QString("Unable to open file: %1").arg(f.source_path);
      }
      return false;
    }

    const qint64 in_size64 = in.size();
    if (in_size64 < 0 || in_size64 > std::numeric_limits<quint32>::max()) {
      if (error) {
        *error = QString("File is too large for %1 format: %2").arg(archive_label, f.source_path);
      }
      return false;
    }

    const qint64 out_offset64 = out.pos();
    if (!ensure_u32_pos(out_offset64, "Archive output exceeds format limits.")) {
      return false;
    }

    qint64 remaining = in_size64;
    while (remaining > 0) {
      const int to_read = static_cast<int>(std::min<qint64>(remaining, buffer.size()));
      const qint64 got = in.read(buffer.data(), to_read);
      if (got <= 0) {
        if (error) {
          *error = QString("Unable to read file: %1").arg(f.source_path);
        }
        return false;
      }
      if (out.write(buffer.constData(), got) != got) {
        if (error) {
          *error = QString("Unable to write destination entry: %1").arg(name);
        }
        return false;
      }
      remaining -= got;
    }

    ArchiveEntry out_entry;
    out_entry.name = name;
    out_entry.offset = static_cast<quint32>(out_offset64);
    out_entry.size = static_cast<quint32>(in_size64);
    dir_entries.push_back(std::move(out_entry));
  }

  const qint64 dir_offset64 = out.pos();
  if (!ensure_u32_pos(dir_offset64, "Archive output exceeds format limits.")) {
    return false;
  }

  const qint64 dir_length64 = static_cast<qint64>(dir_entries.size()) * dir_entry_size;
  if (dir_length64 < 0 || dir_length64 > std::numeric_limits<quint32>::max() ||
      dir_length64 > std::numeric_limits<int>::max()) {
    if (error) {
      *error = QString("%1 directory exceeds format limits.").arg(archive_label);
    }
    return false;
  }

  QByteArray dir;
  dir.resize(static_cast<int>(dir_length64));
  dir.fill('\0');
  for (int i = 0; i < dir_entries.size(); ++i) {
    const ArchiveEntry& e = dir_entries[i];
    const QByteArray name_bytes = e.name.toLatin1();
    const int base = i * dir_entry_size;
    std::memcpy(dir.data() + base, name_bytes.constData(), static_cast<size_t>(name_bytes.size()));
    write_u32_le(&dir, base + name_bytes_limit, e.offset);
    write_u32_le(&dir, base + name_bytes_limit + 4, e.size);
  }

  if (out.write(dir) != dir.size()) {
    if (error) {
      *error = QString("Unable to write %1 directory.").arg(archive_label);
    }
    return false;
  }

  write_u32_le(&header, 4, static_cast<quint32>(dir_offset64));
  write_u32_le(&header, 8, static_cast<quint32>(dir_length64));
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

int run_save_as_cli(const Archive& archive,
                    const CliOptions& options,
                    const QStringList& entry_filters,
                    const QStringList& prefix_filters,
                    QTextStream& out,
                    QTextStream& err) {
  SaveArchiveFormat format = SaveArchiveFormat::Zip;
  QString format_err;
  if (!resolve_save_archive_format(options, &format, &format_err)) {
    err << (format_err.isEmpty() ? "Unable to resolve output archive format.\n" : format_err + "\n");
    return 2;
  }
  if (options.quakelive_encrypt_pk3 && format != SaveArchiveFormat::Zip) {
    err << "--quakelive-encrypt-pk3 is only supported for ZIP-family output formats.\n";
    return 2;
  }

  const QVector<const ArchiveEntry*> selected = select_entries(archive.entries(), entry_filters, prefix_filters, false);
  if (selected.isEmpty()) {
    err << "No file entries matched the save-as selection.\n";
    return 2;
  }

  QTemporaryDir temp;
  QVector<CliDiskFile> files;
  QString collect_err;
  if (!collect_disk_files(archive, selected, &temp, &files, &collect_err)) {
    err << (collect_err.isEmpty() ? "Unable to prepare archive entries.\n" : collect_err + "\n");
    return 2;
  }
  if (files.isEmpty()) {
    err << "No file entries matched the save-as selection.\n";
    return 2;
  }

  const QString dest = QFileInfo(options.save_as_path).absoluteFilePath();
  QString write_err;
  bool ok = false;
  switch (format) {
    case SaveArchiveFormat::Pak:
    case SaveArchiveFormat::Sin:
      ok = write_pak_from_disk_files(dest, files, format == SaveArchiveFormat::Sin, &write_err);
      break;
    case SaveArchiveFormat::Zip: {
      ZipArchive::WritePlan plan;
      plan.disk_files.reserve(files.size());
      for (const CliDiskFile& f : files) {
        ZipArchive::DiskFile zf;
        zf.archive_name = f.archive_name;
        zf.source_path = f.source_path;
        zf.mtime_utc_secs = f.mtime_utc_secs;
        plan.disk_files.push_back(std::move(zf));
      }
      ok = ZipArchive::write_rebuilt(dest, plan, options.quakelive_encrypt_pk3, &write_err);
      break;
    }
    case SaveArchiveFormat::Wad2: {
      WadArchive::WritePlan plan;
      plan.entries.reserve(files.size());
      for (const CliDiskFile& f : files) {
        WadArchive::WriteEntry we;
        we.entry_name = f.archive_name;
        we.source_path = f.source_path;
        plan.entries.push_back(std::move(we));
      }
      ok = WadArchive::write_wad2(dest, plan, &write_err);
      break;
    }
  }

  if (!ok) {
    err << (write_err.isEmpty() ? "Save-as failed.\n" : write_err + "\n");
    return 2;
  }
  out << "Saved: " << dest << "\n";
  out << "Entries: " << files.size() << "\n";
  return 0;
}

int run_convert_cli(const Archive& archive,
                    const CliOptions& options,
                    const QFileInfo& archive_info,
                    const QStringList& entry_filters,
                    const QStringList& prefix_filters,
                    QTextStream& out,
                    QTextStream& err) {
  const QString requested_format = options.convert_format.trimmed();
  QString requested_key = requested_format.toLower();
  if (requested_key.startsWith('.')) {
    requested_key.remove(0, 1);
  }
  const bool convert_idwav = requested_key == "wav";
  const QString format = convert_idwav ? QString("wav") : normalize_image_write_format(requested_format);
  if (!convert_idwav && (format.isEmpty() || !supported_image_write_formats().contains(format))) {
    err << "Unsupported conversion format: " << requested_format << "\n";
    err << "Supported formats: " << supported_image_write_formats().join(", ") << ", wav\n";
    return 2;
  }

  QString out_dir = options.output_dir.trimmed();
  if (out_dir.isEmpty()) {
    const QString base = archive_info.completeBaseName().isEmpty() ? "archive" : archive_info.completeBaseName();
    out_dir = QDir::current().filePath(base + "_" + format);
  }
  QDir od(out_dir);
  if (!od.exists() && !od.mkpath(".")) {
    err << "Unable to create output directory: " << QFileInfo(out_dir).absoluteFilePath() << "\n";
    return 2;
  }

  QVector<const ArchiveEntry*> selected = select_entries(archive.entries(), entry_filters, prefix_filters, false);
  const bool explicit_selection = has_selection_filters(entry_filters, prefix_filters);
  if (!explicit_selection) {
    QVector<const ArchiveEntry*> convertible;
    convertible.reserve(selected.size());
    for (const ArchiveEntry* e : selected) {
      if (!e) {
        continue;
      }
      if (convert_idwav) {
        if (is_idwav_file_name(e->name)) {
          convertible.push_back(e);
        }
      } else if (is_decodable_image_ext(file_ext_lower(e->name))) {
        convertible.push_back(e);
      }
    }
    selected = std::move(convertible);
  }
  if (selected.isEmpty()) {
    err << "No convertible entries matched the conversion input.\n";
    return 2;
  }

  int ok_count = 0;
  int skipped = 0;
  int failed = 0;
  for (const ArchiveEntry* entry : selected) {
    if (!entry) {
      continue;
    }
    const QString name = normalize_archive_entry_name(entry->name);
    if (!is_safe_archive_entry_name(name) || name.endsWith('/')) {
      ++skipped;
      err << "Skipping unsafe entry: " << entry->name << "\n";
      continue;
    }
    if (convert_idwav) {
      if (!is_idwav_file_name(name)) {
        if (explicit_selection) {
          ++failed;
          err << "Convert failed (" << name << "): unsupported IDWAV input format.\n";
        } else {
          ++skipped;
        }
        continue;
      }
    } else if (!is_decodable_image_ext(file_ext_lower(name))) {
      if (explicit_selection) {
        ++failed;
        err << "Convert failed (" << name << "): unsupported image input format.\n";
      } else {
        ++skipped;
      }
      continue;
    }

    QByteArray bytes;
    QString read_err;
    if (!archive.read_entry_bytes(name, &bytes, &read_err)) {
      ++failed;
      err << (read_err.isEmpty() ? QString("Unable to read entry: %1\n").arg(name) : read_err + "\n");
      continue;
    }

    if (convert_idwav) {
      const IdWavDecodeResult decoded = decode_idwav_to_wav_bytes(bytes);
      if (!decoded.ok()) {
        ++failed;
        err << "Convert failed (" << name << "): "
            << (decoded.error.isEmpty() ? "Unable to decode IDWAV audio." : decoded.error) << "\n";
        continue;
      }
      const QString dest = od.filePath(change_file_extension(name, "wav"));
      QString write_err;
      if (!write_bytes_file(dest, decoded.wav_bytes, &write_err)) {
        ++failed;
        err << (write_err.isEmpty() ? QString("Unable to write converted audio: %1\n").arg(dest) : write_err + "\n");
        continue;
      }
      ++ok_count;
      continue;
    }

    const ImageDecodeResult decoded = decode_image_bytes(bytes, name, {});
    if (!decoded.ok()) {
      ++failed;
      err << "Convert failed (" << name << "): "
          << (decoded.error.isEmpty() ? "Unable to decode image." : decoded.error) << "\n";
      continue;
    }

    const QString dest = od.filePath(change_file_extension(name, format));
    ImageWriteOptions write_opts;
    write_opts.format = format;
    write_opts.texture_name = QFileInfo(name).completeBaseName();
    QString write_err;
    if (!write_image_file(decoded.image, dest, write_opts, &write_err)) {
      ++failed;
      err << (write_err.isEmpty() ? QString("Unable to write converted image: %1\n").arg(dest) : write_err + "\n");
      continue;
    }
    ++ok_count;
  }

  out << "Converted: " << ok_count << " file(s)\n";
  if (skipped > 0) {
    out << "Skipped: " << skipped << " item(s)\n";
  }
  if (failed > 0) {
    err << "Failed: " << failed << " item(s)\n";
    return 2;
  }
  return 0;
}

bool output_option_denotes_file(const QString& output_path) {
  if (output_path.trimmed().isEmpty()) {
    return false;
  }
  if (output_path.endsWith('/') || output_path.endsWith('\\')) {
    return false;
  }
  const QFileInfo info(output_path);
  if (info.exists() && info.isDir()) {
    return false;
  }
  return !info.suffix().isEmpty();
}

QString preview_output_path(const QString& output_option, const QString& entry_name, const QString& output_format) {
  if (output_option_denotes_file(output_option)) {
    return QFileInfo(output_option).absoluteFilePath();
  }
  QString out_dir = output_option.trimmed();
  if (out_dir.isEmpty()) {
    out_dir = QDir::currentPath();
  }
  return QDir(out_dir).filePath(change_file_extension(normalize_archive_entry_name(entry_name), output_format));
}

int run_preview_export_cli(const Archive& archive,
                           const CliOptions& options,
                           QTextStream& out,
                           QTextStream& err) {
  const QString name = normalize_archive_entry_name(options.preview_export_entry);
  if (!is_safe_archive_entry_name(name) || name.endsWith('/')) {
    err << "Unsafe --preview-export entry: " << options.preview_export_entry << "\n";
    return 2;
  }

  QByteArray bytes;
  QString read_err;
  if (!archive.read_entry_bytes(name, &bytes, &read_err)) {
    err << (read_err.isEmpty() ? QString("Entry not found: %1\n").arg(name) : read_err + "\n");
    return 2;
  }

  const QString ext = file_ext_lower(name);
  if (ext == "fontdat") {
    const FontDatDecodeResult font = decode_fontdat_bytes(bytes);
    if (!font.ok()) {
      err << (font.error.isEmpty() ? "Unable to parse FONTDAT preview.\n" : font.error + "\n");
      return 2;
    }

    QHash<QString, QString> by_lower;
    by_lower.reserve(archive.entries().size());
    for (const ArchiveEntry& entry : archive.entries()) {
      const QString normalized = normalize_archive_entry_name(entry.name);
      if (!normalized.isEmpty()) {
        by_lower.insert(normalized.toLower(), entry.name);
      }
    }

    QImage atlas;
    QString atlas_name;
    QStringList attempts;
    for (const QString& candidate : fontdat_atlas_candidates_for_path(name)) {
      const QString found = by_lower.value(normalize_archive_entry_name(candidate).toLower());
      if (found.isEmpty()) {
        attempts.push_back(QString("%1: not found").arg(candidate));
        continue;
      }

      QByteArray atlas_bytes;
      QString atlas_err;
      if (!archive.read_entry_bytes(found, &atlas_bytes, &atlas_err, 64LL * 1024 * 1024)) {
        attempts.push_back(QString("%1: %2").arg(found, atlas_err.isEmpty() ? QString("unable to read atlas") : atlas_err));
        continue;
      }
      const ImageDecodeResult decoded = decode_image_bytes(atlas_bytes, QFileInfo(found).fileName(), ImageDecodeOptions{});
      if (!decoded.ok() || decoded.image.isNull()) {
        attempts.push_back(QString("%1: %2").arg(found, decoded.error.isEmpty() ? QString("unable to decode atlas") : decoded.error));
        continue;
      }
      atlas = decoded.image;
      atlas_name = found;
      break;
    }

    if (atlas.isNull()) {
      err << "Unable to resolve FONTDAT atlas image.\n";
      if (!attempts.isEmpty()) {
        err << "Tried:\n- " << attempts.join("\n- ") << "\n";
      }
      return 2;
    }

    FontDatRenderOptions render_options;
    render_options.glyph_scale = (font.font.max_glyph_height() >= 32) ? 1 : 2;
    const FontDatRenderResult rendered = render_fontdat_preview(font.font, atlas, render_options);
    if (!rendered.ok()) {
      err << (rendered.error.isEmpty() ? "Unable to render FONTDAT preview.\n" : rendered.error + "\n");
      return 2;
    }

    QString format = "png";
    if (output_option_denotes_file(options.output_dir)) {
      format = normalize_image_write_format(file_ext_lower(options.output_dir));
      if (format.isEmpty() || !supported_image_write_formats().contains(format)) {
        err << "Unsupported preview image output format: " << file_ext_lower(options.output_dir) << "\n";
        return 2;
      }
    }

    const QString dest = preview_output_path(options.output_dir, name, format);
    ImageWriteOptions write_opts;
    write_opts.format = format;
    write_opts.texture_name = QFileInfo(name).completeBaseName();
    QString write_err;
    if (!write_image_file(rendered.image, dest, write_opts, &write_err)) {
      err << (write_err.isEmpty() ? QString("Unable to write FONTDAT preview export: %1\n").arg(dest) : write_err + "\n");
      return 2;
    }
    out << "Preview exported: " << dest << "\n";
    if (!atlas_name.isEmpty()) {
      out << "FONTDAT atlas: " << atlas_name << "\n";
    }
    return 0;
  }

  if (is_decodable_image_ext(ext)) {
    const ImageDecodeResult decoded = decode_image_bytes(bytes, name, {});
    if (!decoded.ok()) {
      err << (decoded.error.isEmpty() ? "Unable to decode preview image.\n" : decoded.error + "\n");
      return 2;
    }

    QString format = "png";
    if (output_option_denotes_file(options.output_dir)) {
      format = normalize_image_write_format(file_ext_lower(options.output_dir));
      if (format.isEmpty() || !supported_image_write_formats().contains(format)) {
        err << "Unsupported preview image output format: " << file_ext_lower(options.output_dir) << "\n";
        return 2;
      }
    }

    const QString dest = preview_output_path(options.output_dir, name, format);
    ImageWriteOptions write_opts;
    write_opts.format = format;
    write_opts.texture_name = QFileInfo(name).completeBaseName();
    QString write_err;
    if (!write_image_file(decoded.image, dest, write_opts, &write_err)) {
      err << (write_err.isEmpty() ? QString("Unable to write preview export: %1\n").arg(dest) : write_err + "\n");
      return 2;
    }
    out << "Preview exported: " << dest << "\n";
    return 0;
  }

  const QString dest = preview_output_path(options.output_dir, name, looks_like_text(bytes) ? "txt" : ext);
  QString write_err;
  if (!write_bytes_file(dest, bytes, &write_err)) {
    err << (write_err.isEmpty() ? QString("Unable to write preview export: %1\n").arg(dest) : write_err + "\n");
    return 2;
  }
  out << "Preview exported: " << dest << "\n";
  return 0;
}

struct ValidationSummary {
  QStringList errors;
  QStringList warnings;
  int selected_entries = 0;
  int selected_files = 0;
  int selected_dirs = 0;
  int readable_files = 0;
  int dependency_hints = 0;
  int material_files = 0;
  int material_decls = 0;
  int proc_files = 0;
  int proc_material_refs = 0;
};

struct CompareEntry {
  QString path;
  QString sha256;
  quint32 size = 0;
};

struct AssetGraphEdge {
  QString source;
  QString target;
  QString kind;
  bool resolved = false;
};

struct AssetGraphReport {
  QVector<AssetGraphEdge> edges;
  QStringList warnings;
  int entries_scanned = 0;
  int material_files = 0;
  int material_decls = 0;
  int proc_files = 0;
};

struct PackageManifestItem {
  QString path;
  QString sha256;
  quint32 size = 0;
  qint64 mtime_utc_secs = -1;
};

QString hash_bytes_sha256(const QByteArray& bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString normalize_asset_reference(QString ref) {
  ref = ref.trimmed();
  ref.replace('\\', '/');
  const bool had_trailing_slash = ref.endsWith('/');
  while (ref.startsWith('/')) {
    ref.remove(0, 1);
  }
  ref = QDir::cleanPath(ref);
  ref.replace('\\', '/');
  if (ref == ".") {
    ref.clear();
  }
  if (had_trailing_slash && !ref.isEmpty() && !ref.endsWith('/')) {
    ref += '/';
  }
  return ref;
}

QString dot_quote(QString text) {
  text.replace('\\', "\\\\");
  text.replace('"', "\\\"");
  text.replace('\n', "\\n");
  text.replace('\r', "\\r");
  return QString("\"") + text + "\"";
}

bool path_key_starts_with(const QString& path_key, const QString& prefix_key) {
  if (path_key.isEmpty() || prefix_key.isEmpty()) {
    return false;
  }
  return path_key.startsWith(prefix_key, Qt::CaseInsensitive);
}

bool output_path_for_report(const QString& raw, QString* path, QString* error) {
  if (path) {
    path->clear();
  }
  if (error) {
    error->clear();
  }
  const QString trimmed = raw.trimmed();
  if (trimmed.isEmpty()) {
    return false;
  }
  if (trimmed.endsWith('/') || trimmed.endsWith('\\')) {
    if (error) {
      *error = QString("Report output must be a file path, not a directory: %1").arg(trimmed);
    }
    return false;
  }
  const QFileInfo info(trimmed);
  if (info.exists() && info.isDir()) {
    if (error) {
      *error = QString("Report output must be a file path, not a directory: %1").arg(trimmed);
    }
    return false;
  }
  if (path) {
    *path = info.absoluteFilePath();
  }
  return true;
}

int emit_report(const QString& content,
                const CliOptions& options,
                const QString& label,
                QTextStream& out,
                QTextStream& err) {
  QString output_path;
  QString output_err;
  const bool has_output = output_path_for_report(options.output_dir, &output_path, &output_err);
  if (!output_err.isEmpty()) {
    err << output_err << "\n";
    return 2;
  }
  if (!has_output) {
    out << content;
    if (!content.endsWith('\n')) {
      out << "\n";
    }
    return 0;
  }

  QString write_err;
  if (!write_bytes_file(output_path, content.toUtf8(), &write_err)) {
    err << (write_err.isEmpty() ? QString("Unable to write %1 report: %2\n").arg(label, output_path)
                                : write_err + "\n");
    return 2;
  }
  out << label << " written: " << output_path << "\n";
  return 0;
}

QHash<QString, QString> archive_path_lookup(const QVector<ArchiveEntry>& entries) {
  QHash<QString, QString> out;
  out.reserve(entries.size());
  for (const ArchiveEntry& entry : entries) {
    const QString name = normalize_archive_entry_name(entry.name);
    if (name.isEmpty() || !is_safe_archive_entry_name(name)) {
      continue;
    }
    out.insert(name.toLower(), name);
  }
  return out;
}

bool archive_ref_exists(const QHash<QString, QString>& path_by_key, const QString& ref, QString* resolved_path) {
  if (resolved_path) {
    resolved_path->clear();
  }
  QString normalized = normalize_asset_reference(ref);
  if (normalized.isEmpty()) {
    return false;
  }

  const QString lower = normalized.toLower();
  if (normalized.endsWith('/')) {
    for (auto it = path_by_key.constBegin(); it != path_by_key.constEnd(); ++it) {
      if (path_key_starts_with(it.key(), lower)) {
        if (resolved_path) {
          *resolved_path = normalized;
        }
        return true;
      }
    }
    return false;
  }

  if (path_by_key.contains(lower)) {
    if (resolved_path) {
      *resolved_path = path_by_key.value(lower);
    }
    return true;
  }

  if (file_ext_lower(normalized).isEmpty()) {
    static const QStringList kTextureExts = {"dds", "tga", "png", "jpg", "jpeg", "ftx", "wal", "m8", "m32"};
    for (const QString& ext : kTextureExts) {
      const QString candidate = normalized + '.' + ext;
      const QString key = candidate.toLower();
      if (path_by_key.contains(key)) {
        if (resolved_path) {
          *resolved_path = path_by_key.value(key);
        }
        return true;
      }
    }
  }

  return false;
}

void add_graph_edge(AssetGraphReport* report,
                    QSet<QString>* seen,
                    const QString& source,
                    const QString& target,
                    const QString& kind,
                    bool resolved) {
  if (!report || !seen || source.trimmed().isEmpty() || target.trimmed().isEmpty()) {
    return;
  }
  const QString key = source + QChar(0x1f) + target + QChar(0x1f) + kind;
  if (seen->contains(key)) {
    return;
  }
  seen->insert(key);
  AssetGraphEdge edge;
  edge.source = source;
  edge.target = target;
  edge.kind = kind;
  edge.resolved = resolved;
  report->edges.push_back(std::move(edge));
}

QString format_asset_graph_text(const AssetGraphReport& report, const QString& archive_path) {
  QString text;
  QTextStream s(&text);
  s << "Asset graph: " << QFileInfo(archive_path).absoluteFilePath() << "\n";
  s << "Entries scanned: " << report.entries_scanned << "\n";
  s << "Material files: " << report.material_files << "\n";
  s << "Material declarations: " << report.material_decls << "\n";
  s << "PROC files: " << report.proc_files << "\n";
  s << "Edges: " << report.edges.size() << "\n";
  for (const AssetGraphEdge& edge : report.edges) {
    s << edge.source << " -> " << edge.target << " [" << edge.kind
      << (edge.resolved ? ", resolved" : ", unresolved") << "]\n";
  }
  if (!report.warnings.isEmpty()) {
    s << "Warnings:\n";
    for (const QString& warning : report.warnings) {
      s << "  " << warning << "\n";
    }
  }
  return text;
}

QString format_asset_graph_json(const AssetGraphReport& report, const QString& archive_path) {
  QJsonObject root;
  root.insert("schema", "pakfu-asset-graph/v1");
  root.insert("archive", QFileInfo(archive_path).absoluteFilePath());
  QJsonObject counts;
  counts.insert("entries_scanned", report.entries_scanned);
  counts.insert("material_files", report.material_files);
  counts.insert("material_declarations", report.material_decls);
  counts.insert("proc_files", report.proc_files);
  counts.insert("edges", report.edges.size());
  root.insert("counts", counts);

  QJsonArray edges;
  for (const AssetGraphEdge& edge : report.edges) {
    QJsonObject obj;
    obj.insert("source", edge.source);
    obj.insert("target", edge.target);
    obj.insert("kind", edge.kind);
    obj.insert("resolved", edge.resolved);
    edges.append(obj);
  }
  root.insert("edges", edges);

  QJsonArray warnings;
  for (const QString& warning : report.warnings) {
    warnings.append(warning);
  }
  root.insert("warnings", warnings);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString format_asset_graph_dot(const AssetGraphReport& report, const QString& archive_path) {
  QString text;
  QTextStream s(&text);
  s << "digraph PakFuAssetGraph {\n";
  s << "  graph [label=" << dot_quote(QFileInfo(archive_path).fileName()) << ", labelloc=t];\n";
  s << "  node [shape=box, fontname=" << dot_quote("sans-serif") << "];\n";
  for (const AssetGraphEdge& edge : report.edges) {
    s << "  " << dot_quote(edge.source) << " -> " << dot_quote(edge.target)
      << " [label=" << dot_quote(edge.kind)
      << ", color=" << dot_quote(edge.resolved ? "#2e7d32" : "#b71c1c") << "];\n";
  }
  s << "}\n";
  return text;
}

bool build_asset_graph_report(const Archive& archive,
                              const QVector<const ArchiveEntry*>& selected,
                              AssetGraphReport* report,
                              QString* error) {
  if (error) {
    error->clear();
  }
  if (!report) {
    if (error) {
      *error = "Internal error: missing graph report.";
    }
    return false;
  }
  *report = {};

  const QHash<QString, QString> paths = archive_path_lookup(archive.entries());
  QSet<QString> selected_keys;
  selected_keys.reserve(selected.size());
  for (const ArchiveEntry* entry : selected) {
    if (!entry) {
      continue;
    }
    const QString name = normalize_archive_entry_name(entry->name);
    if (!name.isEmpty()) {
      selected_keys.insert(name.toLower());
    }
  }

  QHash<QString, QStringList> material_images_by_name;
  QHash<QString, QString> material_decl_file_by_name;
  QSet<QString> seen_edges;

  const qint64 kMaxMaterialBytes = 8 * 1024 * 1024;
  const qint64 kMaxProcBytes = 96 * 1024 * 1024;

  for (const ArchiveEntry& entry : archive.entries()) {
    const QString name = normalize_archive_entry_name(entry.name);
    if (name.isEmpty() || name.endsWith('/') || !is_safe_archive_entry_name(name)) {
      continue;
    }
    if (file_ext_lower(name) != "mtr") {
      continue;
    }
    if (entry.size > kMaxMaterialBytes) {
      report->warnings.push_back(QString("Skipped large material file: %1").arg(name));
      continue;
    }

    QByteArray bytes;
    QString read_err;
    if (!archive.read_entry_bytes(name, &bytes, &read_err)) {
      report->warnings.push_back(read_err.isEmpty() ? QString("Unable to read material file: %1").arg(name)
                                                    : read_err);
      continue;
    }
    ++report->material_files;
    const IdTech4MaterialParseResult parsed = parse_idtech4_material_bytes(bytes, name);
    if (!parsed.ok()) {
      report->warnings.push_back(parsed.error.isEmpty() ? QString("Unable to parse material file: %1").arg(name)
                                                       : parsed.error);
      continue;
    }
    for (const IdTech4MaterialDecl& decl : parsed.materials) {
      if (decl.name.isEmpty()) {
        continue;
      }
      const QString key = decl.name.toLower();
      material_images_by_name.insert(key, decl.image_refs);
      material_decl_file_by_name.insert(key, name);
      ++report->material_decls;
      if (selected_keys.contains(name.toLower())) {
        const QString material_node = "material:" + decl.name;
        add_graph_edge(report, &seen_edges, name, material_node, "declares-material", true);
        for (const QString& image_ref : decl.image_refs) {
          QString resolved;
          const bool exists = archive_ref_exists(paths, image_ref, &resolved);
          add_graph_edge(report,
                         &seen_edges,
                         material_node,
                         exists ? resolved : normalize_asset_reference(image_ref),
                         "uses-texture",
                         exists);
        }
      }
    }
  }

  ArchiveSearchIndex index;
  ArchiveSearchIndex::BuildInput input;
  input.archive_entries = archive.entries();
  input.scope_label = QFileInfo(archive.path()).fileName();
  index.rebuild(input);
  QHash<QString, ArchiveSearchIndex::Item> items_by_key;
  for (const ArchiveSearchIndex::Item& item : index.items()) {
    items_by_key.insert(item.path.toLower(), item);
  }

  for (const ArchiveEntry* entry : selected) {
    if (!entry) {
      continue;
    }
    const QString name = normalize_archive_entry_name(entry->name);
    if (name.isEmpty() || name.endsWith('/') || !is_safe_archive_entry_name(name)) {
      continue;
    }
    ++report->entries_scanned;

    const ArchiveSearchIndex::Item item = items_by_key.value(name.toLower());
    for (const QString& hint : item.dependency_hints) {
      QString resolved;
      const bool exists = archive_ref_exists(paths, hint, &resolved);
      add_graph_edge(report, &seen_edges, name, exists ? resolved : hint, "dependency-hint", exists);
    }

    if (file_ext_lower(name) != "proc") {
      continue;
    }
    ++report->proc_files;
    if (entry->size > kMaxProcBytes) {
      report->warnings.push_back(QString("Skipped large PROC file: %1").arg(name));
      continue;
    }

    QByteArray bytes;
    QString read_err;
    if (!archive.read_entry_bytes(name, &bytes, &read_err)) {
      report->warnings.push_back(read_err.isEmpty() ? QString("Unable to read PROC file: %1").arg(name) : read_err);
      continue;
    }
    const IdTech4ProcLoadResult proc = load_idtech4_proc_mesh_bytes(bytes, name);
    if (!proc.error.isEmpty()) {
      report->warnings.push_back(proc.error);
      continue;
    }
    for (const QString& material : proc.material_refs) {
      const QString material_node = "material:" + material;
      add_graph_edge(report, &seen_edges, name, material_node, "uses-material", true);
      const QString material_key = material.toLower();
      if (material_decl_file_by_name.contains(material_key)) {
        add_graph_edge(report,
                       &seen_edges,
                       material_decl_file_by_name.value(material_key),
                       material_node,
                       "declares-material",
                       true);
      }
      const QStringList images = material_images_by_name.value(material_key);
      if (images.isEmpty()) {
        add_graph_edge(report, &seen_edges, material_node, material, "unresolved-texture", false);
        continue;
      }
      for (const QString& image_ref : images) {
        QString resolved;
        const bool exists = archive_ref_exists(paths, image_ref, &resolved);
        add_graph_edge(report,
                       &seen_edges,
                       material_node,
                       exists ? resolved : normalize_asset_reference(image_ref),
                       "uses-texture",
                       exists);
      }
    }
  }

  std::sort(report->edges.begin(), report->edges.end(), [](const AssetGraphEdge& a, const AssetGraphEdge& b) {
    const int by_source = a.source.compare(b.source, Qt::CaseInsensitive);
    if (by_source != 0) {
      return by_source < 0;
    }
    const int by_target = a.target.compare(b.target, Qt::CaseInsensitive);
    if (by_target != 0) {
      return by_target < 0;
    }
    return a.kind.compare(b.kind, Qt::CaseInsensitive) < 0;
  });
  report->warnings.removeDuplicates();
  return true;
}

int run_asset_graph_cli(const Archive& archive,
                        const CliOptions& options,
                        const QStringList& entry_filters,
                        const QStringList& prefix_filters,
                        QTextStream& out,
                        QTextStream& err) {
  QString format = options.asset_graph_format.trimmed().toLower();
  if (format.isEmpty()) {
    format = "text";
  }
  if (format != "text" && format != "json" && format != "dot") {
    err << "Unsupported --asset-graph format: " << options.asset_graph_format << "\n";
    err << "Supported formats: text, json, dot\n";
    return 2;
  }

  const QVector<const ArchiveEntry*> selected = select_entries(archive.entries(), entry_filters, prefix_filters, false);
  if (selected.isEmpty()) {
    err << "No file entries matched the asset graph selection.\n";
    return 2;
  }

  AssetGraphReport report;
  QString graph_err;
  if (!build_asset_graph_report(archive, selected, &report, &graph_err)) {
    err << (graph_err.isEmpty() ? "Unable to build asset graph.\n" : graph_err + "\n");
    return 2;
  }

  QString content;
  if (format == "json") {
    content = format_asset_graph_json(report, archive.path());
  } else if (format == "dot") {
    content = format_asset_graph_dot(report, archive.path());
  } else {
    content = format_asset_graph_text(report, archive.path());
  }

  for (const QString& warning : report.warnings) {
    err << "Asset graph warning: " << warning << "\n";
  }
  return emit_report(content, options, "Asset graph", out, err);
}

QString format_package_manifest_text(const QVector<PackageManifestItem>& items,
                                     const Archive& archive,
                                     const QString& selection_label) {
  QString text;
  QTextStream s(&text);
  s << "Package manifest: " << QFileInfo(archive.path()).absoluteFilePath() << "\n";
  s << "Schema: pakfu-package-manifest/v1\n";
  s << "Format: " << archive_format_label(archive) << "\n";
  if (!selection_label.isEmpty()) {
    s << "Selection: " << selection_label << "\n";
  }
  s << "Entries: " << items.size() << "\n";
  s << "SHA256\tSize\tMTimeUtcSecs\tPath\n";
  for (const PackageManifestItem& item : items) {
    s << item.sha256 << "\t" << item.size << "\t" << item.mtime_utc_secs << "\t" << item.path << "\n";
  }
  return text;
}

QString format_package_manifest_json(const QVector<PackageManifestItem>& items,
                                     const Archive& archive,
                                     const QString& selection_label) {
  QJsonObject root;
  root.insert("schema", "pakfu-package-manifest/v1");
  root.insert("archive", QFileInfo(archive.path()).absoluteFilePath());
  root.insert("format", archive_format_label(archive));
  if (!selection_label.isEmpty()) {
    root.insert("selection", selection_label);
  }
  QJsonArray entries;
  for (const PackageManifestItem& item : items) {
    QJsonObject obj;
    obj.insert("path", item.path);
    obj.insert("size", static_cast<double>(item.size));
    obj.insert("mtime_utc_secs", static_cast<double>(item.mtime_utc_secs));
    obj.insert("sha256", item.sha256);
    entries.append(obj);
  }
  root.insert("entries", entries);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString selection_label(const QStringList& entry_filters, const QStringList& prefix_filters) {
  QStringList parts;
  for (const QString& entry : entry_filters) {
    parts.push_back("entry=" + entry);
  }
  for (const QString& prefix : prefix_filters) {
    parts.push_back("prefix=" + prefix);
  }
  return parts.join(", ");
}

int run_package_manifest_cli(const Archive& archive,
                             const CliOptions& options,
                             const QStringList& entry_filters,
                             const QStringList& prefix_filters,
                             QTextStream& out,
                             QTextStream& err) {
  QString format = options.package_manifest_format.trimmed().toLower();
  if (format.isEmpty()) {
    format = "text";
  }
  if (format != "text" && format != "json") {
    err << "Unsupported --package-manifest format: " << options.package_manifest_format << "\n";
    err << "Supported formats: text, json\n";
    return 2;
  }

  QVector<const ArchiveEntry*> selected = select_entries(archive.entries(), entry_filters, prefix_filters, false);
  if (selected.isEmpty()) {
    err << "No file entries matched the package manifest selection.\n";
    return 2;
  }
  std::sort(selected.begin(), selected.end(), [](const ArchiveEntry* a, const ArchiveEntry* b) {
    if (!a || !b) {
      return a != nullptr;
    }
    return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
  });

  QVector<PackageManifestItem> items;
  items.reserve(selected.size());
  for (const ArchiveEntry* entry : selected) {
    if (!entry) {
      continue;
    }
    const QString name = normalize_archive_entry_name(entry->name);
    if (name.isEmpty() || name.endsWith('/') || !is_safe_archive_entry_name(name)) {
      err << "Skipping unsafe manifest entry: " << entry->name << "\n";
      continue;
    }
    QByteArray bytes;
    QString read_err;
    if (!archive.read_entry_bytes(name, &bytes, &read_err)) {
      err << (read_err.isEmpty() ? QString("Unable to read entry for manifest: %1\n").arg(name) : read_err + "\n");
      return 2;
    }
    PackageManifestItem item;
    item.path = name;
    item.size = entry->size;
    item.mtime_utc_secs = entry->mtime_utc_secs;
    item.sha256 = hash_bytes_sha256(bytes);
    items.push_back(std::move(item));
  }

  const QString sel = selection_label(entry_filters, prefix_filters);
  const QString content = format == "json" ? format_package_manifest_json(items, archive, sel)
                                           : format_package_manifest_text(items, archive, sel);
  return emit_report(content, options, "Package manifest", out, err);
}

int run_validate_cli(const Archive& archive,
                     const QStringList& entry_filters,
                     const QStringList& prefix_filters,
                     QTextStream& out,
                     QTextStream& err) {
  ValidationSummary summary;
  constexpr qint64 kMaxValidationMaterialBytes = 8 * 1024 * 1024;
  constexpr qint64 kMaxValidationProcBytes = 96 * 1024 * 1024;
  const QVector<const ArchiveEntry*> selected = select_entries(archive.entries(), entry_filters, prefix_filters, true);
  if (selected.isEmpty()) {
    err << "No entries matched the validation selection.\n";
    return 2;
  }
  summary.selected_entries = selected.size();

  QHash<QString, QString> first_path_by_key;
  first_path_by_key.reserve(selected.size());
  QVector<ArchiveEntry> selected_entries;
  selected_entries.reserve(selected.size());
  QSet<QString> material_names;

  for (const ArchiveEntry* entry : selected) {
    if (!entry) {
      continue;
    }
    QString name = normalize_archive_entry_name(entry->name);
    if (name.isEmpty()) {
      summary.errors.push_back(QString("Empty or invalid entry name: %1").arg(entry->name));
      continue;
    }
    if (name != entry->name) {
      summary.warnings.push_back(QString("Entry name normalizes from \"%1\" to \"%2\".").arg(entry->name, name));
    }
    if (!is_safe_archive_entry_name(name)) {
      summary.errors.push_back(QString("Unsafe entry name: %1").arg(entry->name));
      continue;
    }

    const QString key = name.toLower();
    if (first_path_by_key.contains(key)) {
      summary.warnings.push_back(
        QString("Duplicate normalized entry path: %1 (first seen as %2)").arg(name, first_path_by_key.value(key)));
    } else {
      first_path_by_key.insert(key, name);
    }

    ArchiveEntry normalized_entry = *entry;
    normalized_entry.name = name;
    selected_entries.push_back(normalized_entry);

    if (name.endsWith('/')) {
      ++summary.selected_dirs;
      continue;
    }
    ++summary.selected_files;

    QByteArray probe;
    QString read_err;
    if (!archive.read_entry_bytes(name, &probe, &read_err, 4096)) {
      summary.errors.push_back(read_err.isEmpty() ? QString("Unable to read entry: %1").arg(name) : read_err);
      continue;
    }
    ++summary.readable_files;

    const QString ext = file_ext_lower(name);
    if (ext == "mtr") {
      if (entry->size > kMaxValidationMaterialBytes) {
        summary.warnings.push_back(QString("Skipped large material file during validation: %1").arg(name));
        continue;
      }
      QByteArray bytes;
      if (!archive.read_entry_bytes(name, &bytes, &read_err)) {
        summary.warnings.push_back(read_err.isEmpty() ? QString("Unable to read material file: %1").arg(name) : read_err);
        continue;
      }
      ++summary.material_files;
      const IdTech4MaterialParseResult parsed = parse_idtech4_material_bytes(bytes, name);
      if (!parsed.ok()) {
        summary.warnings.push_back(parsed.error.isEmpty() ? QString("Unable to parse material file: %1").arg(name)
                                                         : parsed.error);
        continue;
      }
      summary.material_decls += parsed.materials.size();
      for (const IdTech4MaterialDecl& decl : parsed.materials) {
        if (!decl.name.isEmpty()) {
          material_names.insert(decl.name.toLower());
        }
      }
    }
  }

  ArchiveSearchIndex index;
  ArchiveSearchIndex::BuildInput input;
  input.archive_entries = selected_entries;
  input.scope_label = QFileInfo(archive.path()).fileName();
  index.rebuild(input);
  for (const ArchiveSearchIndex::Item& item : index.items()) {
    if (!item.is_dir) {
      summary.dependency_hints += item.dependency_hints.size();
    }
  }

  for (const ArchiveEntry& entry : selected_entries) {
    const QString name = normalize_archive_entry_name(entry.name);
    if (name.isEmpty() || name.endsWith('/') || file_ext_lower(name) != "proc") {
      continue;
    }
    ++summary.proc_files;
    if (entry.size > kMaxValidationProcBytes) {
      summary.warnings.push_back(QString("Skipped large PROC file during validation: %1").arg(name));
      continue;
    }
    QByteArray bytes;
    QString read_err;
    if (!archive.read_entry_bytes(name, &bytes, &read_err)) {
      summary.warnings.push_back(read_err.isEmpty() ? QString("Unable to read PROC file: %1").arg(name) : read_err);
      continue;
    }
    const IdTech4ProcLoadResult proc = load_idtech4_proc_mesh_bytes(bytes, name);
    if (!proc.error.isEmpty()) {
      summary.warnings.push_back(proc.error);
      continue;
    }
    summary.proc_material_refs += proc.material_refs.size();
    if (material_names.isEmpty() && !proc.material_refs.isEmpty()) {
      summary.warnings.push_back(QString("%1 references %2 material(s), but no .mtr declarations were found in the validation scope.")
                                   .arg(name)
                                   .arg(proc.material_refs.size()));
      continue;
    }
    int missing = 0;
    for (const QString& material : proc.material_refs) {
      if (!material_names.contains(material.toLower())) {
        ++missing;
      }
    }
    if (missing > 0) {
      summary.warnings.push_back(QString("%1 has %2 unresolved material declaration(s).").arg(name).arg(missing));
    }
  }

  summary.warnings.removeDuplicates();
  summary.errors.removeDuplicates();

  out << "Validation: " << (summary.errors.isEmpty() ? "OK" : "FAILED") << "\n";
  out << "Archive: " << QFileInfo(archive.path()).absoluteFilePath() << "\n";
  out << "Entries checked: " << summary.selected_entries << "\n";
  out << "Files checked: " << summary.selected_files << "\n";
  out << "Directories checked: " << summary.selected_dirs << "\n";
  out << "Readable files: " << summary.readable_files << "\n";
  out << "Dependency hints: " << summary.dependency_hints << "\n";
  out << "Material files/declarations: " << summary.material_files << "/" << summary.material_decls << "\n";
  out << "PROC files/material refs: " << summary.proc_files << "/" << summary.proc_material_refs << "\n";
  out << "Warnings: " << summary.warnings.size() << "\n";
  out << "Errors: " << summary.errors.size() << "\n";
  for (const QString& warning : summary.warnings) {
    err << "Validation warning: " << warning << "\n";
  }
  for (const QString& error : summary.errors) {
    err << "Validation error: " << error << "\n";
  }
  return summary.errors.isEmpty() ? 0 : 2;
}

bool build_compare_map(const Archive& archive,
                       const QStringList& entry_filters,
                       const QStringList& prefix_filters,
                       QHash<QString, CompareEntry>* out_map,
                       QStringList* warnings,
                       QString* error) {
  if (error) {
    error->clear();
  }
  if (out_map) {
    out_map->clear();
  }
  QVector<const ArchiveEntry*> selected = select_entries(archive.entries(), entry_filters, prefix_filters, false);
  if (selected.isEmpty()) {
    return true;
  }
  for (const ArchiveEntry* entry : selected) {
    if (!entry) {
      continue;
    }
    const QString name = normalize_archive_entry_name(entry->name);
    if (name.isEmpty() || name.endsWith('/') || !is_safe_archive_entry_name(name)) {
      if (warnings) {
        warnings->push_back(QString("Skipping unsafe compare entry: %1").arg(entry->name));
      }
      continue;
    }
    QByteArray bytes;
    QString read_err;
    if (!archive.read_entry_bytes(name, &bytes, &read_err)) {
      if (error) {
        *error = read_err.isEmpty() ? QString("Unable to read entry for compare: %1").arg(name) : read_err;
      }
      return false;
    }
    CompareEntry item;
    item.path = name;
    item.size = entry->size;
    item.sha256 = hash_bytes_sha256(bytes);
    const QString key = name.toLower();
    if (out_map && out_map->contains(key) && warnings) {
      warnings->push_back(QString("Duplicate compare entry path: %1").arg(name));
    }
    if (out_map) {
      out_map->insert(key, std::move(item));
    }
  }
  return true;
}

void print_limited_path_list(QTextStream& out, const QString& label, QStringList paths) {
  paths.sort(Qt::CaseInsensitive);
  out << label << ": " << paths.size() << "\n";
  const int limit = std::min(static_cast<int>(paths.size()), 80);
  for (int i = 0; i < limit; ++i) {
    out << "  " << paths[i] << "\n";
  }
  if (paths.size() > limit) {
    out << "  ... (" << (paths.size() - limit) << " more)\n";
  }
}

int run_compare_cli(const Archive& left,
                    const CliOptions& options,
                    const QStringList& entry_filters,
                    const QStringList& prefix_filters,
                    QTextStream& out,
                    QTextStream& err) {
  QFileInfo right_info(options.compare_path);
  if (!right_info.exists()) {
    err << "Compare target not found: " << options.compare_path << "\n";
    return 2;
  }

  Archive right;
  QString load_err;
  if (!right.load(right_info.absoluteFilePath(), &load_err)) {
    err << (load_err.isEmpty() ? "Unable to load compare target.\n" : load_err + "\n");
    return 2;
  }

  QHash<QString, CompareEntry> left_entries;
  QHash<QString, CompareEntry> right_entries;
  QStringList warnings;
  QString compare_err;
  if (!build_compare_map(left, entry_filters, prefix_filters, &left_entries, &warnings, &compare_err) ||
      !build_compare_map(right, entry_filters, prefix_filters, &right_entries, &warnings, &compare_err)) {
    err << (compare_err.isEmpty() ? "Unable to prepare compare entries.\n" : compare_err + "\n");
    return 2;
  }

  QStringList added;
  QStringList removed;
  QStringList changed;
  int unchanged = 0;

  for (auto it = left_entries.constBegin(); it != left_entries.constEnd(); ++it) {
    const CompareEntry right_entry = right_entries.value(it.key());
    if (right_entry.path.isEmpty()) {
      removed.push_back(it.value().path);
      continue;
    }
    if (it.value().size == right_entry.size && it.value().sha256 == right_entry.sha256) {
      ++unchanged;
    } else {
      changed.push_back(it.value().path);
    }
  }
  for (auto it = right_entries.constBegin(); it != right_entries.constEnd(); ++it) {
    if (!left_entries.contains(it.key())) {
      added.push_back(it.value().path);
    }
  }

  const bool different = !added.isEmpty() || !removed.isEmpty() || !changed.isEmpty();
  out << "Compare: " << (different ? "DIFFERENT" : "IDENTICAL") << "\n";
  out << "Left: " << QFileInfo(left.path()).absoluteFilePath() << "\n";
  out << "Right: " << right_info.absoluteFilePath() << "\n";
  out << "Left entries: " << left_entries.size() << "\n";
  out << "Right entries: " << right_entries.size() << "\n";
  out << "Unchanged: " << unchanged << "\n";
  print_limited_path_list(out, "Added in right", added);
  print_limited_path_list(out, "Removed from right", removed);
  print_limited_path_list(out, "Changed", changed);
  for (const QString& warning : warnings) {
    err << "Compare warning: " << warning << "\n";
  }
  return different ? 1 : 0;
}

bool path_is_under_directory(QString path, QString dir) {
  path = QFileInfo(path).absoluteFilePath();
  dir = QFileInfo(dir).absoluteFilePath();
  path.replace('\\', '/');
  dir.replace('\\', '/');
  while (dir.endsWith('/')) {
    dir.chop(1);
  }
  if (path.compare(dir, Qt::CaseInsensitive) == 0) {
    return true;
  }
  return path.startsWith(dir + '/', Qt::CaseInsensitive);
}

QStringList capability_notes(const QStringList& capabilities) {
  QStringList notes;
  for (const QString& cap : capabilities) {
    if (cap == "archive.read") {
      notes.push_back("archive.read: receives archive metadata and paths.");
    } else if (cap == "entries.read") {
      notes.push_back("entries.read: receives selected entries materialized as temporary local files.");
    } else if (cap == "entries.import") {
      notes.push_back("entries.import: may request host-validated generated files for archive write-back.");
    } else {
      notes.push_back(cap + ": custom/unknown capability note.");
    }
  }
  if (notes.isEmpty()) {
    notes.push_back("none: no explicit capabilities requested.");
  }
  return notes;
}

QString extension_executable_display(const ExtensionCommand& command) {
  if (command.argv.isEmpty()) {
    return "(missing executable)";
  }
  const QString exe = command.argv.first();
  if (QFileInfo(exe).isAbsolute() || command.working_directory.isEmpty()) {
    return QDir::cleanPath(exe);
  }
  return QDir::cleanPath(QDir(command.working_directory).filePath(exe));
}

int run_plugin_report_cli(const CliOptions& options, QTextStream& out, QTextStream& err) {
  QVector<ExtensionCommand> commands;
  QStringList warnings;
  QString load_err;
  const QStringList search_dirs = extension_search_dirs_for_options(options);
  if (!load_extension_commands(search_dirs, &commands, &warnings, &load_err)) {
    err << (load_err.isEmpty() ? "Unable to load extension manifests.\n" : load_err + "\n");
    return 2;
  }

  const QStringList default_dirs = default_extension_search_dirs();
  out << "Extension report\n";
  out << "Host capabilities: " << extension_host_capabilities().join(", ") << "\n";
  out << "Search dirs:\n";
  for (const QString& dir : search_dirs) {
    const bool custom = std::none_of(default_dirs.cbegin(), default_dirs.cend(), [&](const QString& default_dir) {
      return QFileInfo(dir).absoluteFilePath().compare(QFileInfo(default_dir).absoluteFilePath(), Qt::CaseInsensitive) == 0;
    });
    out << "  " << dir << (custom ? " (custom)" : "") << "\n";
  }
  out << "Commands: " << commands.size() << "\n";
  for (const ExtensionCommand& command : commands) {
    bool default_location = false;
    for (const QString& dir : default_dirs) {
      if (path_is_under_directory(command.manifest_path, dir)) {
        default_location = true;
        break;
      }
    }

    out << "- " << extension_command_ref(command) << "  " << extension_command_display_name(command) << "\n";
    if (!command.command_description.isEmpty()) {
      out << "  Description: " << command.command_description << "\n";
    }
    out << "  Manifest: " << command.manifest_path << "\n";
    out << "  Working directory: " << command.working_directory << "\n";
    out << "  Executable: " << extension_executable_display(command) << "\n";
    out << "  Trust scope: " << (default_location ? "default extension directory" : "custom or external directory") << "\n";
    out << "  Entries: " << (command.requires_entries ? "required" : "optional")
        << ", multiple: " << (command.allow_multiple ? "yes" : "no") << "\n";
    if (!command.allowed_extensions.isEmpty()) {
      out << "  Allowed extensions: " << command.allowed_extensions.join(", ") << "\n";
    }
    out << "  Capability notes:\n";
    for (const QString& note : capability_notes(command.capabilities)) {
      out << "    " << note << "\n";
    }
  }

  out << "Manifest warnings: " << warnings.size() << "\n";
  for (const QString& warning : warnings) {
    err << "Extension warning: " << warning << "\n";
  }
  return 0;
}

int run_platform_report_cli(const CliOptions& options, QTextStream& out, QTextStream& err) {
  Q_UNUSED(options);
  out << "PakFu platform report\n";
  out << "Version: " << PAKFU_VERSION << "\n";
  out << "Qt: " << qVersion() << "\n";
  out << "Qt plugins: " << QLibraryInfo::path(QLibraryInfo::PluginsPath) << "\n";
  out << "OS: " << QSysInfo::prettyProductName() << "\n";
  out << "Kernel: " << QSysInfo::kernelType() << ' ' << QSysInfo::kernelVersion() << "\n";
  out << "CPU: current=" << QSysInfo::currentCpuArchitecture()
      << ", build=" << QSysInfo::buildCpuArchitecture() << "\n";
  out << "Application dir: " << QCoreApplication::applicationDirPath() << "\n";
  out << "App data: " << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) << "\n";
  out << "App local data: " << QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) << "\n";
  out << "App config: " << QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) << "\n";
  out << "Documents: " << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) << "\n";

  out << "Extension dirs:\n";
  for (const QString& dir : default_extension_search_dirs()) {
    out << "  " << dir << (QFileInfo(dir).isDir() ? " (present)" : " (missing)") << "\n";
  }

  QVector<ExtensionCommand> commands;
  QStringList extension_warnings;
  QString extension_err;
  if (load_extension_commands(default_extension_search_dirs(), &commands, &extension_warnings, &extension_err)) {
    out << "Extension commands: " << commands.size() << "\n";
    out << "Extension manifest warnings: " << extension_warnings.size() << "\n";
  } else {
    err << (extension_err.isEmpty() ? "Unable to load extension manifests.\n" : extension_err + "\n");
  }

  QString game_err;
  const GameSetState state = load_game_set_state(&game_err);
  if (!game_err.isEmpty()) {
    err << game_err << "\n";
  } else {
    out << "Configured installations: " << state.sets.size() << "\n";
    const GameSet* selected = find_game_set(state, state.selected_uid);
    const QString selected_name =
      selected ? (selected->name.isEmpty() ? game_display_name(selected->game) : selected->name) : QString();
    out << "Selected installation: "
        << (selected ? QString("%1 [%2]").arg(selected_name, game_id_key(selected->game))
                     : QString("(none)"))
        << "\n";
  }

  out << "Shell integration:\n";
#if defined(Q_OS_WIN)
  out << "  Windows: Open With registration is user-managed under HKCU via the File Associations dialog.\n";
#elif defined(Q_OS_MACOS)
  out << "  macOS: document types are declared in the app bundle Info.plist.\n";
#elif defined(Q_OS_LINUX)
  out << "  Linux: desktop entry, shared-mime-info, AppStream metadata, and hicolor icon are installed with packages.\n";
#else
  out << "  Platform: generic file-open support through Qt and explicit CLI paths.\n";
#endif
  out << "Release assets: installer and portable package per platform plus pakfu-<version>-release-manifest.json.\n";
  return 0;
}

int run_list_plugins_cli(const CliOptions& options, QTextStream& out, QTextStream& err) {
  QVector<ExtensionCommand> commands;
  QStringList warnings;
  QString load_err;
  const QStringList search_dirs = extension_search_dirs_for_options(options);
  if (!load_extension_commands(search_dirs, &commands, &warnings, &load_err)) {
    err << (load_err.isEmpty() ? "Unable to load extension manifests.\n" : load_err + "\n");
    return 2;
  }

  out << "Extension search dirs:\n";
  for (const QString& dir : search_dirs) {
    out << "  " << dir << "\n";
  }
  if (commands.isEmpty()) {
    out << "No extensions found.\n";
  } else {
    out << "Extensions:\n";
    for (const ExtensionCommand& command : commands) {
      out << extension_command_ref(command) << "\t" << extension_command_display_name(command);
      if (!command.command_description.isEmpty()) {
        out << "\t" << command.command_description;
      }
      out << "\n";
      out << "  entries: " << (command.requires_entries ? "required" : "optional");
      out << ", multiple: " << (command.allow_multiple ? "yes" : "no");
      if (!command.allowed_extensions.isEmpty()) {
        out << ", extensions: " << command.allowed_extensions.join(", ");
      }
      out << ", capabilities: "
          << (command.capabilities.isEmpty() ? QString("none") : command.capabilities.join(", "));
      out << "\n";
    }
  }

  for (const QString& warning : warnings) {
    err << "Extension warning: " << warning << "\n";
  }
  return 0;
}

bool mount_archive_entry(const Archive& source,
                         const QString& entry,
                         QTemporaryDir* temp,
                         Archive* mounted,
                         QString* error) {
  if (!temp || !temp->isValid() || !mounted) {
    if (error) {
      *error = "Unable to create temporary mount directory.";
    }
    return false;
  }
  const QString name = normalize_archive_entry_name(entry);
  if (!is_safe_archive_entry_name(name) || name.endsWith('/')) {
    if (error) {
      *error = QString("Unsafe --mount entry: %1").arg(entry);
    }
    return false;
  }

  QString leaf = QFileInfo(name).fileName();
  if (leaf.isEmpty()) {
    leaf = "mounted.archive";
  }
  const QString mounted_path = QDir(temp->path()).filePath(leaf);
  QString extract_err;
  if (!source.extract_entry_to_file(name, mounted_path, &extract_err)) {
    if (error) {
      *error = extract_err.isEmpty() ? QString("Unable to extract mounted entry: %1").arg(name) : extract_err;
    }
    return false;
  }
  QString load_err;
  if (!mounted->load(mounted_path, &load_err)) {
    if (error) {
      *error = load_err.isEmpty() ? QString("Unable to load mounted archive: %1").arg(name) : load_err;
    }
    return false;
  }
  if (error) {
    error->clear();
  }
  return true;
}

QString describe_game_set_line(const GameSet& set, bool selected) {
  QString line;
  line += selected ? "* " : "  ";
  line += set.uid.isEmpty() ? "(missing-uid)" : set.uid;
  line += "  ";
  line += set.name.isEmpty() ? game_display_name(set.game) : set.name;
  line += "  [" + game_id_key(set.game) + "]";
  if (!set.default_dir.isEmpty()) {
    line += "  default=" + QFileInfo(set.default_dir).absoluteFilePath();
  }
  if (!set.root_dir.isEmpty()) {
    line += "  root=" + QFileInfo(set.root_dir).absoluteFilePath();
  }
  return line;
}

QString installation_primary_label(const GameSet& set) {
  return set.name.isEmpty() ? game_display_name(set.game) : set.name;
}

bool installation_less(const GameSet* a, const GameSet* b) {
  if (!a || !b) {
    return a != nullptr;
  }
  const QString a_primary = installation_primary_label(*a);
  const QString b_primary = installation_primary_label(*b);
  const int by_primary = QString::compare(a_primary, b_primary, Qt::CaseInsensitive);
  if (by_primary != 0) {
    return by_primary < 0;
  }

  const QString a_game = game_display_name(a->game);
  const QString b_game = game_display_name(b->game);
  const int by_game = QString::compare(a_game, b_game, Qt::CaseInsensitive);
  if (by_game != 0) {
    return by_game < 0;
  }

  return QString::compare(a->uid, b->uid, Qt::CaseInsensitive) < 0;
}

int apply_auto_detect_to_state(GameSetState& state, QStringList* log) {
  const GameAutoDetectResult detected = auto_detect_supported_games();
  if (log) {
    *log = detected.log;
  }

  int changes = 0;
  for (const DetectedGameInstall& install : detected.installs) {
    GameSet* existing = nullptr;
    for (GameSet& set : state.sets) {
      if (set.game == install.game) {
        existing = &set;
        break;
      }
    }

    if (existing) {
      existing->root_dir = install.root_dir;
      existing->default_dir = install.default_dir;
      if (!install.launch.executable_path.isEmpty()) {
        existing->launch.executable_path = install.launch.executable_path;
      }
      if (!install.launch.working_dir.isEmpty()) {
        existing->launch.working_dir = install.launch.working_dir;
      }
      if (existing->palette_id.isEmpty()) {
        existing->palette_id = default_palette_for_game(existing->game);
      }
      if (existing->name.isEmpty()) {
        existing->name = game_display_name(existing->game);
      }
      ++changes;
      continue;
    }

    GameSet set;
    set.uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    set.game = install.game;
    set.name = game_display_name(set.game);
    set.root_dir = install.root_dir;
    set.default_dir = install.default_dir;
    set.palette_id = default_palette_for_game(set.game);
    set.launch = install.launch;
    state.sets.push_back(set);
    ++changes;
  }

  if (state.selected_uid.isEmpty() && !state.sets.isEmpty()) {
    state.selected_uid = state.sets.first().uid;
  }

  return changes;
}

const GameSet* find_game_set_by_selector(const GameSetState& state, const QString& selector, QString* error) {
  if (error) {
    error->clear();
  }
  const QString s = selector.trimmed();
  if (s.isEmpty()) {
    if (error) {
      *error = "Empty game set selector.";
    }
    return nullptr;
  }

  if (const GameSet* by_uid = find_game_set(state, s)) {
    return by_uid;
  }

  QVector<const GameSet*> matches;
  matches.reserve(state.sets.size());
  for (const GameSet& set : state.sets) {
    const QString key = game_id_key(set.game);
    const QString display = game_display_name(set.game);
    const QString name = set.name;
    if (key.compare(s, Qt::CaseInsensitive) == 0 ||
        display.compare(s, Qt::CaseInsensitive) == 0 ||
        name.compare(s, Qt::CaseInsensitive) == 0) {
      matches.push_back(&set);
    }
  }

  if (matches.isEmpty()) {
    if (error) {
      *error = "Game set not found: " + s;
    }
    return nullptr;
  }
  if (matches.size() > 1) {
    if (error) {
      *error = "Game set selector is ambiguous: " + s;
    }
    return nullptr;
  }
  return matches.first();
}
}  // namespace

bool wants_cli(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    const QString key = arg.section('=', 0, 0);
    if (key == "--cli" || key == "--list" || key == "-l" ||
        key == "--info" || key == "-i" ||
        key == "--extract" || key == "-x" ||
        key == "--entry" || key == "--prefix" || key == "--mount" ||
        key == "--save-as" || key == "--format" || key == "--quakelive-encrypt-pk3" ||
        key == "--validate" || key == "--compare" || key == "--asset-graph" ||
        key == "--package-manifest" || key == "--convert" || key == "--preview-export" ||
        key == "--check-updates" || key == "--update-repo" || key == "--update-channel" ||
        key == "--platform-report" || key == "--plugin-report" ||
        key == "--list-game-sets" || key == "--auto-detect-game-sets" || key == "--select-game-set" ||
        key == "--list-game-installs" || key == "--auto-detect-game-installs" || key == "--select-game-install" ||
        key == "--qa-practical" || key == "--output" || key == "-o" ||
        key == "--help" || key == "-h" || key == "--help-all" ||
        key == "--version" || key == "-v") {
      return true;
    }
  }
  return false;
}

bool cli_requires_gui(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    const QString key = arg.section('=', 0, 0);
    if (key == "--qa-practical") {
      return true;
    }
  }
  return false;
}

CliParseResult parse_cli(QCoreApplication& app, CliOptions& options, QString* output) {
  QCommandLineParser parser;
  parser.setApplicationDescription("PakFu command-line interface");
  parser.addHelpOption();
  parser.addVersionOption();

  const QCommandLineOption cli_option("cli", "Run in CLI mode (no UI).");
  const QCommandLineOption list_option({"l", "list"}, "List entries in the archive.");
  const QCommandLineOption info_option({"i", "info"}, "Show archive summary information.");
  const QCommandLineOption extract_option({"x", "extract"}, "Extract archive contents.");
  const QCommandLineOption entry_option("entry", "Limit list/extract/save-as/convert/run-plugin to this exact archive entry (repeatable).", "path");
  const QCommandLineOption prefix_option("prefix", "Limit list/extract/save-as/convert/run-plugin to entries under this archive prefix (repeatable).", "dir");
  const QCommandLineOption mount_option("mount", "Mount a nested archive entry before running the requested action.", "entry");
  const QCommandLineOption save_as_option("save-as", "Write selected entries to a new archive.", "archive");
  const QCommandLineOption format_option("format", "Archive format for --save-as (pak, sin, zip, pk3, pk4, pkz, wad, wad2).", "format");
  const QCommandLineOption quakelive_encrypt_option("quakelive-encrypt-pk3", "Encrypt ZIP-family --save-as output as a Quake Live Beta PK3.");
  const QCommandLineOption validate_option("validate", "Validate archive safety, readability, duplicates, and known asset relationships.");
  const QCommandLineOption compare_option("compare", "Compare the loaded archive with another archive or folder (returns 1 when content differs).", "archive");
  const QCommandLineOption asset_graph_option("asset-graph", "Export an asset dependency graph (text, json, dot).", "format");
  const QCommandLineOption package_manifest_option("package-manifest", "Export a reproducibility manifest with entry sizes and SHA-256 hashes (text, json).", "format");
  const QCommandLineOption convert_option("convert", "Convert selected image entries to an output image format.", "format");
  const QCommandLineOption preview_export_option("preview-export", "Export the preview rendition of one entry (images/FONTDAT become image files; text/binary falls back to bytes).", "entry");
  const QCommandLineOption list_plugins_option("list-plugins", "List discovered extension commands.");
  const QCommandLineOption plugin_report_option("plugin-report", "Print extension discovery, trust, and capability diagnostics.");
  const QCommandLineOption run_plugin_option("run-plugin", "Run an extension command against the current archive.", "plugin[:command]");
  const QCommandLineOption plugin_dir_option("plugin-dir", "Add an extension search directory (repeatable).", "dir");
  const QCommandLineOption check_updates_option("check-updates", "Check GitHub for new releases.");
  const QCommandLineOption platform_report_option("platform-report", "Print platform, install-path, extension, and shell-integration diagnostics.");
  const QCommandLineOption qa_practical_option(
    "qa-practical",
    "Run practical archive-ops UI QA checks (selection/marquee/modifier smoke tests).");
  const QCommandLineOption list_game_sets_option("list-game-sets", "List configured installations (legacy option).");
  const QCommandLineOption list_game_installs_option("list-game-installs", "List configured installations.");
  const QCommandLineOption auto_detect_game_sets_option(
    "auto-detect-game-sets",
    "Auto-detect supported games (Steam → GOG.com → EOS) and create/update installations (legacy option).");
  const QCommandLineOption auto_detect_game_installs_option(
    "auto-detect-game-installs",
    "Auto-detect supported games (Steam → GOG.com → EOS) and create/update installations.");
  const QCommandLineOption select_game_set_option(
    "select-game-set",
    "Select the active installation (by UID, game key, or name) (legacy option).",
    "selector");
  const QCommandLineOption select_game_install_option(
    "select-game-install",
    "Select the active installation (by UID, game key, or name).",
    "selector");
  const QCommandLineOption update_repo_option(
    "update-repo",
    "Override the GitHub repo used for update checks (owner/name).",
    "repo");
  const QCommandLineOption update_channel_option(
    "update-channel",
    "Override the update channel (full releases only; prereleases are ignored).",
    "channel");
  const QCommandLineOption output_option(
    {"o", "output"},
    "Output directory for extraction/conversion/preview export, or output file for reports and one preview export.",
    "path");

  parser.addOption(cli_option);
  parser.addOption(list_option);
  parser.addOption(info_option);
  parser.addOption(extract_option);
  parser.addOption(entry_option);
  parser.addOption(prefix_option);
  parser.addOption(mount_option);
  parser.addOption(save_as_option);
  parser.addOption(format_option);
  parser.addOption(quakelive_encrypt_option);
  parser.addOption(validate_option);
  parser.addOption(compare_option);
  parser.addOption(asset_graph_option);
  parser.addOption(package_manifest_option);
  parser.addOption(convert_option);
  parser.addOption(preview_export_option);
  parser.addOption(list_plugins_option);
  parser.addOption(plugin_report_option);
  parser.addOption(run_plugin_option);
  parser.addOption(plugin_dir_option);
  parser.addOption(check_updates_option);
  parser.addOption(platform_report_option);
  parser.addOption(qa_practical_option);
  parser.addOption(list_game_sets_option);
  parser.addOption(list_game_installs_option);
  parser.addOption(auto_detect_game_sets_option);
  parser.addOption(auto_detect_game_installs_option);
  parser.addOption(select_game_set_option);
  parser.addOption(select_game_install_option);
  parser.addOption(update_repo_option);
  parser.addOption(update_channel_option);
  parser.addOption(output_option);
  parser.addPositionalArgument("archive", "Path to an archive or folder (PAK/SIN/PK3/PK4/PKZ/ZIP/RESOURCES/WAD/WAD2/WAD3).");

  if (!parser.parse(app.arguments())) {
    if (output) {
      *output = normalize_output(parser.errorText()) + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (parser.isSet("help")) {
    if (output) {
      *output = parser.helpText();
    }
    return CliParseResult::ExitOk;
  }

  if (parser.isSet("version")) {
    if (output) {
      *output = normalize_output(app.applicationName() + ' ' + app.applicationVersion());
    }
    return CliParseResult::ExitOk;
  }

  options.list = parser.isSet(list_option);
  options.info = parser.isSet(info_option);
  options.extract = parser.isSet(extract_option);
  options.entry_filters = parser.values(entry_option);
  options.prefix_filters = parser.values(prefix_option);
  options.mount_entry = parser.value(mount_option);
  options.save_as = parser.isSet(save_as_option);
  options.save_as_path = parser.value(save_as_option);
  options.save_format = parser.value(format_option);
  options.quakelive_encrypt_pk3 = parser.isSet(quakelive_encrypt_option);
  options.validate = parser.isSet(validate_option);
  options.compare_path = parser.value(compare_option);
  options.asset_graph_format = parser.value(asset_graph_option);
  if (parser.isSet(asset_graph_option) && options.asset_graph_format.trimmed().isEmpty()) {
    options.asset_graph_format = "text";
  }
  options.package_manifest_format = parser.value(package_manifest_option);
  if (parser.isSet(package_manifest_option) && options.package_manifest_format.trimmed().isEmpty()) {
    options.package_manifest_format = "text";
  }
  options.convert_format = parser.value(convert_option);
  options.preview_export_entry = parser.value(preview_export_option);
  options.list_plugins = parser.isSet(list_plugins_option);
  options.plugin_report = parser.isSet(plugin_report_option);
  options.run_plugin = parser.value(run_plugin_option);
  options.plugin_dirs = parser.values(plugin_dir_option);
  options.check_updates = parser.isSet(check_updates_option);
  options.platform_report = parser.isSet(platform_report_option);
  options.qa_practical = parser.isSet(qa_practical_option);
  options.list_game_sets = parser.isSet(list_game_sets_option) || parser.isSet(list_game_installs_option);
  options.auto_detect_game_sets = parser.isSet(auto_detect_game_sets_option) || parser.isSet(auto_detect_game_installs_option);
  options.select_game_set = parser.value(select_game_set_option);
  if (options.select_game_set.isEmpty()) {
    options.select_game_set = parser.value(select_game_install_option);
  }
  options.output_dir = parser.value(output_option);
  options.update_repo = parser.value(update_repo_option);
  options.update_channel = parser.value(update_channel_option);

  const QStringList positional = parser.positionalArguments();
  if (!positional.isEmpty()) {
    options.pak_path = positional.first();
  }

  const bool any_archive_action = options.list || options.info || options.extract || options.save_as ||
                                  options.validate || !options.compare_path.isEmpty() ||
                                  !options.asset_graph_format.isEmpty() ||
                                  !options.package_manifest_format.isEmpty() ||
                                  !options.convert_format.isEmpty() || !options.preview_export_entry.isEmpty() ||
                                  !options.run_plugin.isEmpty();
  const bool any_action = any_archive_action || options.list_plugins || options.plugin_report ||
                          options.platform_report || options.check_updates ||
                          options.qa_practical ||
                          options.list_game_sets || options.auto_detect_game_sets ||
                          !options.select_game_set.isEmpty();
  if (!any_action && options.pak_path.isEmpty()) {
    if (output) {
      *output = parser.helpText();
    }
    return CliParseResult::ExitOk;
  }

  if (!any_action && !options.pak_path.isEmpty()) {
    options.info = true;
  }

  if (any_archive_action && options.pak_path.isEmpty()) {
    if (output) {
      *output = normalize_output("Missing archive path.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if ((parser.isSet(entry_option) || parser.isSet(prefix_option)) &&
      !(options.list || options.extract || options.save_as || options.validate ||
        !options.compare_path.isEmpty() || !options.asset_graph_format.isEmpty() ||
        !options.package_manifest_format.isEmpty() || !options.convert_format.isEmpty() ||
        !options.run_plugin.isEmpty())) {
    if (output) {
      *output = normalize_output("--entry/--prefix can be used with archive actions such as --list, --extract, --validate, --compare, --asset-graph, --package-manifest, --save-as, --convert, or --run-plugin.") +
                '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (parser.isSet(run_plugin_option) && options.run_plugin.trimmed().isEmpty()) {
    if (output) {
      *output = normalize_output("--run-plugin requires a plugin or plugin:command value.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (parser.isSet(compare_option) && options.compare_path.trimmed().isEmpty()) {
    if (output) {
      *output = normalize_output("--compare requires an archive or folder path.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (!options.plugin_dirs.isEmpty() && !options.list_plugins && !options.plugin_report &&
      options.run_plugin.trimmed().isEmpty()) {
    if (output) {
      *output = normalize_output("--plugin-dir can be used with --list-plugins, --plugin-report, or --run-plugin.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (options.save_as && options.save_as_path.trimmed().isEmpty()) {
    if (output) {
      *output = normalize_output("--save-as requires an output archive path.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (parser.isSet(format_option) && !options.save_as) {
    if (output) {
      *output = normalize_output("--format is only valid with --save-as.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (options.quakelive_encrypt_pk3 && !options.save_as) {
    if (output) {
      *output = normalize_output("--quakelive-encrypt-pk3 is only valid with --save-as.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (parser.isSet(convert_option) && options.convert_format.trimmed().isEmpty()) {
    if (output) {
      *output = normalize_output("--convert requires an output image format.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (parser.isSet(preview_export_option) && options.preview_export_entry.trimmed().isEmpty()) {
    if (output) {
      *output = normalize_output("--preview-export requires an archive entry path.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if (options.qa_practical) {
    const bool has_conflict = options.list || options.info || options.extract || options.save_as ||
                              options.validate || !options.compare_path.isEmpty() ||
                              !options.asset_graph_format.isEmpty() ||
                              !options.package_manifest_format.isEmpty() ||
                              !options.convert_format.isEmpty() || !options.preview_export_entry.isEmpty() ||
                              options.list_plugins || options.plugin_report || options.platform_report ||
                              !options.run_plugin.isEmpty() ||
                              !options.mount_entry.isEmpty() || options.check_updates ||
                              options.list_game_sets || options.auto_detect_game_sets ||
                              !options.select_game_set.isEmpty() || !options.pak_path.isEmpty();
    if (has_conflict) {
      if (output) {
        *output = normalize_output("--qa-practical must be used by itself.") + '\n' + parser.helpText();
      }
      return CliParseResult::ExitError;
    }
  }

  return CliParseResult::Ok;
}

int run_cli(const CliOptions& options) {
  QTextStream out(stdout);
  QTextStream err(stderr);
  const bool has_archive_action = options.list || options.info || options.extract || options.save_as ||
                                  options.validate || !options.compare_path.isEmpty() ||
                                  !options.asset_graph_format.isEmpty() ||
                                  !options.package_manifest_format.isEmpty() ||
                                  !options.convert_format.isEmpty() ||
                                  !options.preview_export_entry.isEmpty() ||
                                  !options.run_plugin.isEmpty();

  if (options.qa_practical) {
    out << "Running practical archive-ops QA...\n";
    out.flush();
    return run_practical_archive_ops_qa();
  }

  if (options.platform_report) {
    const int rc = run_platform_report_cli(options, out, err);
    if (rc != 0 || (!has_archive_action && !options.list_plugins && !options.plugin_report &&
                    !options.check_updates && !options.list_game_sets &&
                    !options.auto_detect_game_sets && options.select_game_set.isEmpty())) {
      return rc;
    }
  }

  if (options.list_game_sets || options.auto_detect_game_sets || !options.select_game_set.isEmpty()) {
    QString load_err;
    GameSetState state = load_game_set_state(&load_err);
    if (!load_err.isEmpty()) {
      err << load_err << "\n";
      return 2;
    }

    if (options.auto_detect_game_sets) {
      QStringList log;
      const int changes = apply_auto_detect_to_state(state, &log);
      QString save_err;
      if (!save_game_set_state(state, &save_err)) {
        err << (save_err.isEmpty() ? "Failed to save game sets.\n" : save_err + "\n");
        return 2;
      }
      out << "Auto-detect: " << changes << " change(s)\n";
      if (!log.isEmpty()) {
        for (const QString& line : log) {
          out << line << "\n";
        }
      }
    }

    if (!options.select_game_set.isEmpty()) {
      QString sel_err;
      const GameSet* selected = find_game_set_by_selector(state, options.select_game_set, &sel_err);
      if (!selected) {
        err << (sel_err.isEmpty() ? "Game set not found.\n" : sel_err + "\n");
        return 2;
      }
      state.selected_uid = selected->uid;
      QString save_err;
      if (!save_game_set_state(state, &save_err)) {
        err << (save_err.isEmpty() ? "Failed to save game sets.\n" : save_err + "\n");
        return 2;
      }
      out << "Selected Game Set:\n";
      out << describe_game_set_line(*selected, true) << "\n";
    }

    if (options.list_game_sets) {
      if (state.sets.isEmpty()) {
        out << "No Game Sets configured.\n";
        return 0;
      }
      QVector<const GameSet*> sorted;
      sorted.reserve(state.sets.size());
      for (const GameSet& set : state.sets) {
        sorted.push_back(&set);
      }
      std::sort(sorted.begin(), sorted.end(), installation_less);

      for (const GameSet* set : sorted) {
        if (!set) {
          continue;
        }
        out << describe_game_set_line(*set, set->uid == state.selected_uid) << "\n";
      }
    }

    if (options.list_game_sets || options.auto_detect_game_sets || !options.select_game_set.isEmpty()) {
      return 0;
    }
  }

  if (options.check_updates) {
    UpdateService updater;
    const QString repo = options.update_repo.isEmpty() ? PAKFU_GITHUB_REPO : options.update_repo;
    const QString channel = options.update_channel.isEmpty() ? PAKFU_UPDATE_CHANNEL : options.update_channel;
    updater.configure(repo, channel, PAKFU_VERSION);
    const UpdateCheckResult result = updater.check_for_updates_sync();
    switch (result.state) {
      case UpdateCheckState::UpdateAvailable:
        out << "Update available: " << result.info.version << "\n";
        if (!result.info.asset_name.isEmpty()) {
          out << "Asset: " << result.info.asset_name << "\n";
        }
        if (result.info.html_url.isValid()) {
          out << "Release: " << result.info.html_url.toString() << "\n";
        }
        return 0;
      case UpdateCheckState::UpToDate:
        out << "PakFu is up to date.\n";
        return 0;
      case UpdateCheckState::NoRelease:
        err << "No releases found.\n";
        return 2;
      case UpdateCheckState::NotConfigured:
        err << "Update repo not configured.\n";
        return 2;
      case UpdateCheckState::Error:
        err << (result.message.isEmpty() ? "Update check failed.\n" : result.message + '\n');
        return 2;
    }
  }

  if (options.list_plugins) {
    const int rc = run_list_plugins_cli(options, out, err);
    if (rc != 0 || (options.run_plugin.isEmpty() && !options.plugin_report)) {
      return rc;
    }
  }

  if (options.plugin_report) {
    const int rc = run_plugin_report_cli(options, out, err);
    if (rc != 0 || options.run_plugin.isEmpty()) {
      return rc;
    }
  }

  if (options.pak_path.isEmpty()) {
    err << "No archive path provided.\n";
    return 2;
  }

  QFileInfo archive_info(options.pak_path);
  if (!archive_info.exists()) {
    err << "Archive not found: " << options.pak_path << "\n";
    return 2;
  }

  Archive archive;
  QString load_err;
  if (!archive.load(archive_info.absoluteFilePath(), &load_err)) {
    err << (load_err.isEmpty() ? "Unable to load archive.\n" : load_err + "\n");
    return 2;
  }

  QTemporaryDir mounted_temp;
  Archive mounted_archive;
  Archive* active_archive = &archive;
  QFileInfo active_info = archive_info;
  if (!options.mount_entry.isEmpty()) {
    QString mount_err;
    if (!mount_archive_entry(archive, options.mount_entry, &mounted_temp, &mounted_archive, &mount_err)) {
      err << (mount_err.isEmpty() ? "Unable to mount nested archive.\n" : mount_err + "\n");
      return 2;
    }
    active_archive = &mounted_archive;
    active_info = QFileInfo(mounted_archive.path());
  }

  QStringList entry_filters;
  QStringList prefix_filters;
  QString filter_err;
  if (!normalize_selection_filters(options, &entry_filters, &prefix_filters, &filter_err)) {
    err << (filter_err.isEmpty() ? "Invalid entry selection.\n" : filter_err + "\n");
    return 2;
  }

  const QVector<ArchiveEntry>& entries = active_archive->entries();

  if (options.info) {
    out << "Archive: " << QFileInfo(active_archive->path()).absoluteFilePath() << "\n";
    if (!options.mount_entry.isEmpty()) {
      out << "Mounted from: " << options.mount_entry << "\n";
    }
    if (active_archive->readable_path() != active_archive->path()) {
      out << "Readable: " << QFileInfo(active_archive->readable_path()).absoluteFilePath() << "\n";
    }
    out << "Format: " << format_string(active_archive->format()) << "\n";
    if (active_archive->format() == Archive::Format::Wad) {
      if (active_archive->is_doom_wad()) {
        out << "WAD type: IWAD/PWAD (Doom-family)\n";
      } else if (active_archive->is_wad3()) {
        out << "WAD type: WAD3 (Quake/Half-Life)\n";
      } else {
        out << "WAD type: WAD2 (Quake)\n";
      }
    }
    if (active_archive->is_quakelive_encrypted_pk3()) {
      out << "Quake Live encrypted PK3: yes\n";
    }
    out << "Entries: " << entries.size() << "\n";

    quint64 total = 0;
    for (const ArchiveEntry& e : entries) {
      if (!e.name.endsWith('/')) {
        total += static_cast<quint64>(e.size);
      }
    }
    out << "Total uncompressed: " << total << " bytes\n";
  }

  if (options.list) {
    QVector<const ArchiveEntry*> sorted;
    if (has_selection_filters(entry_filters, prefix_filters)) {
      sorted = select_entries(entries, entry_filters, prefix_filters, true);
    } else {
      sorted.reserve(entries.size());
      for (const ArchiveEntry& e : entries) {
        sorted.push_back(&e);
      }
    }
    std::sort(sorted.begin(), sorted.end(), [](const ArchiveEntry* a, const ArchiveEntry* b) {
      if (!a || !b) {
        return a != nullptr;
      }
      return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
    });
    for (const ArchiveEntry* e : sorted) {
      if (!e) {
        continue;
      }
      out << e->size << "\t" << e->name << "\n";
    }
  }

  if (options.extract) {
    QString out_dir = options.output_dir.trimmed();
    if (out_dir.isEmpty()) {
      const QString base = active_info.completeBaseName().isEmpty() ? "archive" : active_info.completeBaseName();
      out_dir = QDir::current().filePath(base + "_extract");
    }
    QDir od(out_dir);
    if (!od.exists() && !od.mkpath(".")) {
      err << "Unable to create output directory: " << QFileInfo(out_dir).absoluteFilePath() << "\n";
      return 2;
    }

    int ok = 0;
    int failed = 0;
    int skipped = 0;

    const QVector<const ArchiveEntry*> targets = select_entries(entries, entry_filters, prefix_filters, true);
    if (targets.isEmpty() && has_selection_filters(entry_filters, prefix_filters)) {
      err << "No entries matched the extraction selection.\n";
      return 2;
    }

    for (const ArchiveEntry* entry : targets) {
      if (!entry) {
        continue;
      }
      const QString name = normalize_archive_entry_name(entry->name);
      if (!is_safe_archive_entry_name(name)) {
        ++skipped;
        err << "Skipping unsafe entry: " << entry->name << "\n";
        continue;
      }

      const QString dest = od.filePath(name);
      if (name.endsWith('/')) {
        QDir d(dest);
        if (!d.exists() && !d.mkpath(".")) {
          ++failed;
          err << "Unable to create directory: " << dest << "\n";
        }
        continue;
      }

      QString ex_err;
      if (!active_archive->extract_entry_to_file(name, dest, &ex_err)) {
        ++failed;
        err << (ex_err.isEmpty() ? "Extract failed: " + name + "\n" : ex_err + "\n");
        continue;
      }
      ++ok;
    }

    out << "Extracted: " << ok << " file(s)\n";
    if (skipped > 0) {
      out << "Skipped: " << skipped << " unsafe entr" << (skipped == 1 ? "y" : "ies") << "\n";
    }
    if (failed > 0) {
      err << "Failed: " << failed << " item(s)\n";
      return 2;
    }
  }

  if (options.validate) {
    const int rc = run_validate_cli(*active_archive, entry_filters, prefix_filters, out, err);
    if (rc != 0) {
      return rc;
    }
  }

  if (!options.asset_graph_format.isEmpty()) {
    const int rc = run_asset_graph_cli(*active_archive, options, entry_filters, prefix_filters, out, err);
    if (rc != 0) {
      return rc;
    }
  }

  if (!options.package_manifest_format.isEmpty()) {
    const int rc = run_package_manifest_cli(*active_archive, options, entry_filters, prefix_filters, out, err);
    if (rc != 0) {
      return rc;
    }
  }

  if (options.save_as) {
    const int rc = run_save_as_cli(*active_archive, options, entry_filters, prefix_filters, out, err);
    if (rc != 0) {
      return rc;
    }
  }

  if (!options.compare_path.isEmpty()) {
    const int rc = run_compare_cli(*active_archive, options, entry_filters, prefix_filters, out, err);
    if (rc != 0) {
      return rc;
    }
  }

  if (!options.convert_format.isEmpty()) {
    const int rc = run_convert_cli(*active_archive, options, active_info, entry_filters, prefix_filters, out, err);
    if (rc != 0) {
      return rc;
    }
  }

  if (!options.preview_export_entry.isEmpty()) {
    const int rc = run_preview_export_cli(*active_archive, options, out, err);
    if (rc != 0) {
      return rc;
    }
  }

  if (!options.run_plugin.isEmpty()) {
    QVector<ExtensionCommand> commands;
    QStringList warnings;
    QString load_err;
    if (!load_extension_commands(extension_search_dirs_for_options(options), &commands, &warnings, &load_err)) {
      err << (load_err.isEmpty() ? "Unable to load extension manifests.\n" : load_err + "\n");
      return 2;
    }
    for (const QString& warning : warnings) {
      err << "Extension warning: " << warning << "\n";
    }

    QString command_err;
    const ExtensionCommand* command = find_extension_command(commands, options.run_plugin, &command_err);
    if (!command) {
      err << (command_err.isEmpty() ? "Extension command not found.\n" : command_err + "\n");
      return 2;
    }

    QTemporaryDir extension_temp;
    if (!extension_temp.isValid()) {
      err << "Unable to create extension temporary directory.\n";
      return 2;
    }
    QVector<ExtensionEntryContext> extension_entries;
    if (extension_command_has_capability(*command, "entries.read")) {
      QString selection_err;
      if (!collect_extension_entries(*active_archive,
                                     entry_filters,
                                     prefix_filters,
                                     &extension_temp,
                                     &extension_entries,
                                     &selection_err)) {
        err << (selection_err.isEmpty() ? "Unable to prepare extension input files.\n" : selection_err + "\n");
        return 2;
      }
    }

    ExtensionRunContext context;
    context.archive_path = active_archive->path();
    context.readable_archive_path = active_archive->readable_path();
    context.archive_format = archive_format_label(*active_archive);
    context.mounted_entry = options.mount_entry;
    context.quakelive_encrypted_pk3 = active_archive->is_quakelive_encrypted_pk3();
    context.wad3 = active_archive->is_wad3();
    context.doom_wad = active_archive->is_doom_wad();
    context.entries = std::move(extension_entries);
    if (extension_command_has_capability(*command, "entries.import")) {
      context.import_root = QDir(extension_temp.path()).filePath("imports");
      if (!QDir().mkpath(context.import_root)) {
        err << "Unable to create extension import directory: " << context.import_root << "\n";
        return 2;
      }
    }

    ExtensionRunResult result;
    QString run_err;
    if (!run_extension_command(*command, context, &result, &run_err)) {
      err << (run_err.isEmpty() ? "Extension command failed.\n" : run_err + "\n");
      if (!result.std_err.trimmed().isEmpty()) {
        err << result.std_err;
        if (!result.std_err.endsWith('\n')) {
          err << "\n";
        }
      }
      if (!result.std_out.trimmed().isEmpty()) {
        out << result.std_out;
        if (!result.std_out.endsWith('\n')) {
          out << "\n";
        }
      }
      return 2;
    }

    out << "Extension completed: " << extension_command_ref(*command) << "\n";
    if (!result.std_out.trimmed().isEmpty()) {
      out << result.std_out;
      if (!result.std_out.endsWith('\n')) {
        out << "\n";
      }
    }
    if (!result.std_err.trimmed().isEmpty()) {
      err << result.std_err;
      if (!result.std_err.endsWith('\n')) {
        err << "\n";
      }
    }
    if (!result.imports.isEmpty()) {
      extension_temp.setAutoRemove(false);
      out << "Extension imports requested: " << result.imports.size() << " file(s)\n";
      out << "Import files retained in: " << extension_temp.path() << "\n";
      for (const ExtensionImportEntry& import : result.imports) {
        out << "  " << import.archive_name << " <- " << import.local_path << "\n";
      }
      out << "CLI write-back is not applied automatically; use the GUI archive tab or consume the reported import paths.\n";
    }
  }

  return 0;
}
