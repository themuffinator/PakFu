#include "cli.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>

#include "archive/archive.h"
#include "archive/path_safety.h"
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
    "dds", "ftx", "jpg", "jpeg", "lmp", "mip", "pcx", "png", "swl", "tga", "wal",
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
        key == "--convert" || key == "--preview-export" ||
        key == "--check-updates" || key == "--update-repo" || key == "--update-channel" ||
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
  const QCommandLineOption entry_option("entry", "Limit list/extract/save-as/convert to this exact archive entry (repeatable).", "path");
  const QCommandLineOption prefix_option("prefix", "Limit list/extract/save-as/convert to entries under this archive prefix (repeatable).", "dir");
  const QCommandLineOption mount_option("mount", "Mount a nested archive entry before running the requested action.", "entry");
  const QCommandLineOption save_as_option("save-as", "Write selected entries to a new archive.", "archive");
  const QCommandLineOption format_option("format", "Archive format for --save-as (pak, sin, zip, pk3, pk4, pkz, wad, wad2).", "format");
  const QCommandLineOption quakelive_encrypt_option("quakelive-encrypt-pk3", "Encrypt ZIP-family --save-as output as a Quake Live Beta PK3.");
  const QCommandLineOption convert_option("convert", "Convert selected image entries to an output image format.", "format");
  const QCommandLineOption preview_export_option("preview-export", "Export the preview rendition of one entry (images become image files; text/binary falls back to bytes).", "entry");
  const QCommandLineOption check_updates_option("check-updates", "Check GitHub for new releases.");
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
    "Output directory for extraction/conversion/preview export, or output file for one preview export.",
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
  parser.addOption(convert_option);
  parser.addOption(preview_export_option);
  parser.addOption(check_updates_option);
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
  options.convert_format = parser.value(convert_option);
  options.preview_export_entry = parser.value(preview_export_option);
  options.check_updates = parser.isSet(check_updates_option);
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
                                  !options.convert_format.isEmpty() || !options.preview_export_entry.isEmpty();
  const bool any_action = any_archive_action || options.check_updates ||
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

  if ((options.list || options.info || options.extract || options.save_as ||
       !options.convert_format.isEmpty() || !options.preview_export_entry.isEmpty()) &&
      options.pak_path.isEmpty()) {
    if (output) {
      *output = normalize_output("Missing archive path.") + '\n' + parser.helpText();
    }
    return CliParseResult::ExitError;
  }

  if ((parser.isSet(entry_option) || parser.isSet(prefix_option)) &&
      !(options.list || options.extract || options.save_as || !options.convert_format.isEmpty())) {
    if (output) {
      *output = normalize_output("--entry/--prefix can be used with --list, --extract, --save-as, or --convert.") +
                '\n' + parser.helpText();
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
                              !options.convert_format.isEmpty() || !options.preview_export_entry.isEmpty() ||
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

  if (options.qa_practical) {
    out << "Running practical archive-ops QA...\n";
    out.flush();
    return run_practical_archive_ops_qa();
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

  if (options.save_as) {
    const int rc = run_save_as_cli(*active_archive, options, entry_filters, prefix_filters, out, err);
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

  return 0;
}
