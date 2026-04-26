#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "wad/wad_archive.h"
#include "zip/zip_archive.h"

namespace {
void fail_message(const QString& message) {
  QTextStream(stderr) << message << '\n';
}

bool write_file(const QString& path, const QByteArray& bytes, QString* error) {
  const QFileInfo info(path);
  if (!info.dir().exists() && !QDir().mkpath(info.dir().absolutePath())) {
    if (error) {
      *error = QString("Unable to create directory: %1").arg(info.dir().absolutePath());
    }
    return false;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error) {
      *error = QString("Unable to write fixture: %1").arg(path);
    }
    return false;
  }
  if (file.write(bytes) != bytes.size()) {
    if (error) {
      *error = QString("Unable to write fixture bytes: %1").arg(path);
    }
    return false;
  }
  return true;
}

bool expect_bytes(ZipArchive& archive, const QString& name, const QByteArray& expected, QString* error) {
  QByteArray actual;
  if (!archive.read_entry_bytes(name, &actual, error)) {
    return false;
  }
  if (actual != expected) {
    if (error) {
      *error = QString("ZIP entry bytes did not round-trip: %1").arg(name);
    }
    return false;
  }
  return true;
}

bool expect_bytes(WadArchive& archive, const QString& name, const QByteArray& expected, QString* error) {
  QByteArray actual;
  if (!archive.read_entry_bytes(name, &actual, error)) {
    return false;
  }
  if (actual != expected) {
    if (error) {
      *error = QString("WAD entry bytes did not round-trip: %1").arg(name);
    }
    return false;
  }
  return true;
}

bool test_zip_writer_roundtrip(const QString& root, QString* error) {
  const QString alpha_path = QDir(root).filePath("alpha.txt");
  const QString old_path = QDir(root).filePath("old.txt");
  const QString nested_path = QDir(root).filePath("nested/keep.bin");
  if (!write_file(alpha_path, "alpha", error) ||
      !write_file(old_path, "old", error) ||
      !write_file(nested_path, QByteArray("\x01\x02\x03", 3), error)) {
    return false;
  }

  const QString source_zip = QDir(root).filePath("source.pk3");
  ZipArchive::WritePlan initial;
  initial.explicit_directories.push_back("empty/");
  initial.disk_files.push_back(ZipArchive::DiskFile{"alpha.txt", alpha_path, 100});
  initial.disk_files.push_back(ZipArchive::DiskFile{"old.txt", old_path, 101});
  initial.disk_files.push_back(ZipArchive::DiskFile{"nested/keep.bin", nested_path, 102});
  if (!ZipArchive::write_rebuilt(source_zip, initial, false, error)) {
    return false;
  }

  const QString beta_path = QDir(root).filePath("beta.txt");
  const QString added_path = QDir(root).filePath("added/new.txt");
  if (!write_file(beta_path, "beta", error) ||
      !write_file(added_path, "new", error)) {
    return false;
  }

  const QString out_zip = QDir(root).filePath("rebuilt.pk3");
  ZipArchive::WritePlan rebuild;
  rebuild.source_zip_path = source_zip;
  rebuild.deleted_files.insert("old.txt");
  rebuild.replaced_entries.insert("alpha.txt");
  rebuild.explicit_directories.push_back("extra/");
  rebuild.disk_files.push_back(ZipArchive::DiskFile{"alpha.txt", beta_path, 200});
  rebuild.disk_files.push_back(ZipArchive::DiskFile{"added/new.txt", added_path, 201});
  if (!ZipArchive::write_rebuilt(out_zip, rebuild, false, error)) {
    return false;
  }

  ZipArchive out;
  if (!out.load(out_zip, error)) {
    return false;
  }
  if (!expect_bytes(out, "alpha.txt", "beta", error) ||
      !expect_bytes(out, "nested/keep.bin", QByteArray("\x01\x02\x03", 3), error) ||
      !expect_bytes(out, "added/new.txt", "new", error)) {
    return false;
  }

  QByteArray ignored;
  QString read_error;
  if (out.read_entry_bytes("old.txt", &ignored, &read_error)) {
    if (error) {
      *error = "Deleted ZIP entry is still present.";
    }
    return false;
  }
  if (!out.read_entry_bytes("extra/", &ignored, error)) {
    return false;
  }
  return true;
}

bool test_wad_writer_roundtrip(const QString& root, QString* error) {
  const QString stone_path = QDir(root).filePath("stone.mip");
  const QString palette_path = QDir(root).filePath("palette.lmp");
  if (!write_file(stone_path, "mip-bytes", error) ||
      !write_file(palette_path, QByteArray(768, '\x2a'), error)) {
    return false;
  }

  const QString source_wad = QDir(root).filePath("source.wad");
  WadArchive::WritePlan initial;
  initial.entries.push_back(WadArchive::WriteEntry{"stone.mip", stone_path, false});
  initial.entries.push_back(WadArchive::WriteEntry{"palette.lmp", palette_path, false});
  if (!WadArchive::write_wad2(source_wad, initial, error)) {
    return false;
  }

  WadArchive source;
  if (!source.load(source_wad, error) ||
      !expect_bytes(source, "stone.mip", "mip-bytes", error) ||
      !expect_bytes(source, "palette.lmp", QByteArray(768, '\x2a'), error)) {
    return false;
  }

  const QString pic_path = QDir(root).filePath("pic.lmp");
  QByteArray pic_bytes;
  pic_bytes.append(char(2));
  pic_bytes.append(char(0));
  pic_bytes.append(char(0));
  pic_bytes.append(char(0));
  pic_bytes.append(char(2));
  pic_bytes.append(char(0));
  pic_bytes.append(char(0));
  pic_bytes.append(char(0));
  pic_bytes.append("abcd", 4);
  if (!write_file(pic_path, pic_bytes, error)) {
    return false;
  }

  const QString rebuilt_wad = QDir(root).filePath("rebuilt.wad");
  WadArchive::WritePlan rebuild;
  rebuild.source_wad_path = source_wad;
  rebuild.entries.push_back(WadArchive::WriteEntry{"stone.mip", QString(), true});
  rebuild.entries.push_back(WadArchive::WriteEntry{"pic.lmp", pic_path, false});
  if (!WadArchive::write_wad2(rebuilt_wad, rebuild, error)) {
    return false;
  }

  WadArchive out;
  if (!out.load(rebuilt_wad, error) ||
      !expect_bytes(out, "stone.mip", "mip-bytes", error) ||
      !expect_bytes(out, "pic.lmp", pic_bytes, error)) {
    return false;
  }

  QByteArray ignored;
  QString read_error;
  if (out.read_entry_bytes("palette.lmp", &ignored, &read_error)) {
    if (error) {
      *error = "Omitted WAD entry is still present.";
    }
    return false;
  }
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  Q_UNUSED(app);

  QTemporaryDir temp;
  if (!temp.isValid()) {
    fail_message("Unable to create temporary test directory.");
    return 1;
  }

  QString error;
  if (!test_zip_writer_roundtrip(QDir(temp.path()).filePath("zip"), &error)) {
    fail_message(error.isEmpty() ? "ZIP writer round-trip failed." : error);
    return 1;
  }
  if (!test_wad_writer_roundtrip(QDir(temp.path()).filePath("wad"), &error)) {
    fail_message(error.isEmpty() ? "WAD writer round-trip failed." : error);
    return 1;
  }

  return 0;
}
