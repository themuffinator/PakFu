#include <algorithm>
#include <cstring>

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include "archive/archive.h"
#include "formats/image_loader.h"
#include "formats/image_writer.h"
#include "pak/pak_archive.h"
#include "wad/wad_archive.h"
#include "zip/zip_archive.h"

namespace {
constexpr int kPakHeaderSize = 12;
constexpr int kWadHeaderSize = 12;
constexpr int kQ12WadDirEntrySize = 32;
constexpr int kDoomWadDirEntrySize = 16;

void fail_message(const QString& message) {
	QTextStream(stderr) << message << '\n';
}

void set_error(QString* error, const QString& message) {
	if (error) {
		*error = message;
	}
}

void append_u32_le(QByteArray* bytes, quint32 value) {
	bytes->append(static_cast<char>(value & 0xFFu));
	bytes->append(static_cast<char>((value >> 8u) & 0xFFu));
	bytes->append(static_cast<char>((value >> 16u) & 0xFFu));
	bytes->append(static_cast<char>((value >> 24u) & 0xFFu));
}

void append_u32_be(QByteArray* bytes, quint32 value) {
	bytes->append(static_cast<char>((value >> 24u) & 0xFFu));
	bytes->append(static_cast<char>((value >> 16u) & 0xFFu));
	bytes->append(static_cast<char>((value >> 8u) & 0xFFu));
	bytes->append(static_cast<char>(value & 0xFFu));
}

void write_u32_le(QByteArray* bytes, int offset, quint32 value) {
	(*bytes)[offset + 0] = static_cast<char>(value & 0xFFu);
	(*bytes)[offset + 1] = static_cast<char>((value >> 8u) & 0xFFu);
	(*bytes)[offset + 2] = static_cast<char>((value >> 16u) & 0xFFu);
	(*bytes)[offset + 3] = static_cast<char>((value >> 24u) & 0xFFu);
}

void write_u16_le(QByteArray* bytes, int offset, quint16 value) {
	(*bytes)[offset + 0] = static_cast<char>(value & 0xFFu);
	(*bytes)[offset + 1] = static_cast<char>((value >> 8u) & 0xFFu);
}

bool write_file(const QString& path, const QByteArray& bytes, QString* error) {
	const QFileInfo info(path);
	if (!info.dir().exists() && !QDir().mkpath(info.dir().absolutePath())) {
		set_error(error, QString("Unable to create directory: %1").arg(info.dir().absolutePath()));
		return false;
	}

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		set_error(error, QString("Unable to write fixture: %1").arg(path));
		return false;
	}
	if (file.write(bytes) != bytes.size()) {
		set_error(error, QString("Unable to write fixture bytes: %1").arg(path));
		return false;
	}
	return true;
}

bool read_file(const QString& path, QByteArray* out, QString* error) {
	if (out) {
		out->clear();
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		set_error(error, QString("Unable to read file: %1").arg(path));
		return false;
	}
	if (out) {
		*out = file.readAll();
	}
	return true;
}

bool append_fixed_name(QByteArray* bytes, const QString& name, int byte_count, QString* error) {
	const QByteArray latin1 = name.toLatin1();
	if (QString::fromLatin1(latin1) != name) {
		set_error(error, QString("Fixture entry name must be Latin-1: %1").arg(name));
		return false;
	}
	if (latin1.size() > byte_count) {
		set_error(error, QString("Fixture entry name is too long: %1").arg(name));
		return false;
	}

	const int offset = bytes->size();
	bytes->resize(offset + byte_count);
	std::memset(bytes->data() + offset, 0, static_cast<size_t>(byte_count));
	if (!latin1.isEmpty()) {
		std::memcpy(bytes->data() + offset, latin1.constData(), static_cast<size_t>(latin1.size()));
	}
	return true;
}

QString format_name(Archive::Format format) {
	switch (format) {
		case Archive::Format::Directory:
			return "directory";
		case Archive::Format::Pak:
			return "PAK/SIN";
		case Archive::Format::Wad:
			return "WAD";
		case Archive::Format::Resources:
			return "resources";
		case Archive::Format::Zip:
			return "ZIP-family";
		case Archive::Format::Unknown:
			break;
	}
	return "unknown";
}

bool expect_archive_entry(const QString& archive_path,
                          Archive::Format expected_format,
                          const QString& entry_name,
                          const QByteArray& expected_bytes,
                          QString* error,
                          bool expect_quakelive = false,
                          bool expect_wad3 = false,
                          bool expect_doom_wad = false) {
	Archive archive;
	if (!archive.load(archive_path, error)) {
		return false;
	}
	if (archive.format() != expected_format) {
		set_error(error,
		          QString("Fixture loaded as %1, expected %2: %3")
		            .arg(format_name(archive.format()), format_name(expected_format), archive_path));
		return false;
	}
	if (archive.is_quakelive_encrypted_pk3() != expect_quakelive) {
		set_error(error, QString("Quake Live PK3 detection mismatch: %1").arg(archive_path));
		return false;
	}
	if (archive.is_wad3() != expect_wad3) {
		set_error(error, QString("WAD3 detection mismatch: %1").arg(archive_path));
		return false;
	}
	if (archive.is_doom_wad() != expect_doom_wad) {
		set_error(error, QString("Doom WAD detection mismatch: %1").arg(archive_path));
		return false;
	}

	const auto entries = archive.entries();
	const bool listed = std::any_of(entries.cbegin(), entries.cend(), [&](const ArchiveEntry& entry) {
		return entry.name == entry_name;
	});
	if (!listed) {
		set_error(error, QString("Fixture entry was not listed: %1 in %2").arg(entry_name, archive_path));
		return false;
	}

	QByteArray bytes;
	if (!archive.read_entry_bytes(entry_name, &bytes, error)) {
		return false;
	}
	if (bytes != expected_bytes) {
		set_error(error, QString("Fixture entry bytes mismatch: %1 in %2").arg(entry_name, archive_path));
		return false;
	}

	QString safe_name = entry_name;
	safe_name.replace('/', '_');
	const QString extract_path =
	  QFileInfo(archive_path).absoluteDir().filePath(QString("extract-%1").arg(safe_name));
	if (!archive.extract_entry_to_file(entry_name, extract_path, error)) {
		return false;
	}

	QByteArray extracted;
	if (!read_file(extract_path, &extracted, error)) {
		return false;
	}
	if (extracted != expected_bytes) {
		set_error(error, QString("Fixture extraction bytes mismatch: %1 in %2").arg(entry_name, archive_path));
		return false;
	}
	return true;
}

bool write_pak_fixture(const QString& path,
                       const QByteArray& signature,
                       int name_bytes,
                       int dir_entry_size,
                       const QString& entry_name,
                       const QByteArray& payload,
                       QString* error) {
	if (signature.size() != 4) {
		set_error(error, "PAK fixture signature must be four bytes.");
		return false;
	}
	if (entry_name.toLatin1().size() > name_bytes) {
		set_error(error, QString("PAK fixture entry name is too long: %1").arg(entry_name));
		return false;
	}

	QByteArray bytes(kPakHeaderSize, '\0');
	std::memcpy(bytes.data(), signature.constData(), 4);
	const quint32 payload_offset = kPakHeaderSize;
	bytes.append(payload);
	const quint32 dir_offset = static_cast<quint32>(bytes.size());

	QByteArray dir(dir_entry_size, '\0');
	const QByteArray name = entry_name.toLatin1();
	std::memcpy(dir.data(), name.constData(), static_cast<size_t>(name.size()));
	write_u32_le(&dir, name_bytes, payload_offset);
	write_u32_le(&dir, name_bytes + 4, static_cast<quint32>(payload.size()));
	bytes.append(dir);

	write_u32_le(&bytes, 4, dir_offset);
	write_u32_le(&bytes, 8, static_cast<quint32>(dir.size()));
	return write_file(path, bytes, error);
}

bool write_resources_fixture(const QString& path,
                             const QString& entry_name,
                             const QByteArray& payload,
                             QString* error) {
	QByteArray toc;
	append_u32_be(&toc, 1);
	append_u32_le(&toc, static_cast<quint32>(entry_name.toUtf8().size()));
	toc.append(entry_name.toUtf8());
	append_u32_be(&toc, 12);
	append_u32_be(&toc, static_cast<quint32>(payload.size()));

	QByteArray bytes;
	append_u32_be(&bytes, 0xD000000Du);
	append_u32_be(&bytes, static_cast<quint32>(12 + payload.size()));
	append_u32_be(&bytes, static_cast<quint32>(toc.size()));
	bytes.append(payload);
	bytes.append(toc);
	return write_file(path, bytes, error);
}

bool write_q12_wad_fixture(const QString& path,
                           const QByteArray& magic,
                           const QString& lump_name,
                           quint8 lump_type,
                           const QByteArray& payload,
                           QString* error) {
	if (magic.size() != 4) {
		set_error(error, "WAD fixture magic must be four bytes.");
		return false;
	}

	QByteArray bytes(kWadHeaderSize, '\0');
	std::memcpy(bytes.data(), magic.constData(), 4);
	write_u32_le(&bytes, 4, 1);
	bytes.append(payload);
	const quint32 dir_offset = static_cast<quint32>(bytes.size());

	QByteArray dir;
	append_u32_le(&dir, kWadHeaderSize);
	append_u32_le(&dir, static_cast<quint32>(payload.size()));
	append_u32_le(&dir, static_cast<quint32>(payload.size()));
	dir.append(static_cast<char>(lump_type));
	dir.append('\0');
	dir.append('\0');
	dir.append('\0');
	if (!append_fixed_name(&dir, lump_name, 16, error)) {
		return false;
	}
	if (dir.size() != kQ12WadDirEntrySize) {
		set_error(error, "Generated WAD2/WAD3 directory entry has an invalid size.");
		return false;
	}
	bytes.append(dir);
	write_u32_le(&bytes, 8, dir_offset);
	return write_file(path, bytes, error);
}

bool write_doom_wad_fixture(const QString& path,
                            const QString& lump_name,
                            const QByteArray& payload,
                            QString* error) {
	QByteArray bytes(kWadHeaderSize, '\0');
	std::memcpy(bytes.data(), "PWAD", 4);
	write_u32_le(&bytes, 4, 1);
	bytes.append(payload);
	const quint32 dir_offset = static_cast<quint32>(bytes.size());

	QByteArray dir;
	append_u32_le(&dir, kWadHeaderSize);
	append_u32_le(&dir, static_cast<quint32>(payload.size()));
	if (!append_fixed_name(&dir, lump_name, 8, error)) {
		return false;
	}
	if (dir.size() != kDoomWadDirEntrySize) {
		set_error(error, "Generated Doom WAD directory entry has an invalid size.");
		return false;
	}
	bytes.append(dir);
	write_u32_le(&bytes, 8, dir_offset);
	return write_file(path, bytes, error);
}

QByteArray one_pixel_tga() {
	QByteArray bytes(18, '\0');
	bytes[2] = 2;  // Uncompressed true-color image.
	write_u16_le(&bytes, 12, 1);
	write_u16_le(&bytes, 14, 1);
	bytes[16] = 24;
	bytes[17] = 0x20;  // Top-left origin.
	bytes.append('\0');  // B
	bytes.append('\0');  // G
	bytes.append(static_cast<char>(0xFF));  // R
	return bytes;
}

bool test_directory_fixture(const QString& root, QString* error) {
	const QString folder = QDir(root).filePath("folder");
	const QString entry = "scripts/test.cfg";
	const QByteArray payload = "set fixture 1\n";
	if (!write_file(QDir(folder).filePath(entry), payload, error)) {
		return false;
	}
	return expect_archive_entry(folder, Archive::Format::Directory, entry, payload, error);
}

bool test_pak_fixture(const QString& root, QString* error) {
	const QString entry = "scripts/test.cfg";
	const QByteArray payload = "pak fixture\n";
	const QString pak_path = QDir(root).filePath("fixture.pak");
	if (!write_pak_fixture(pak_path, "PACK", 56, 64, entry, payload, error) ||
	    !expect_archive_entry(pak_path, Archive::Format::Pak, entry, payload, error)) {
		return false;
	}

	PakArchive pak;
	const QString rebuilt_path = QDir(root).filePath("fixture-copy.pak");
	if (!pak.load(pak_path, error) || !pak.save_as(rebuilt_path, error)) {
		return false;
	}
	return expect_archive_entry(rebuilt_path, Archive::Format::Pak, entry, payload, error);
}

bool test_sin_fixture(const QString& root, QString* error) {
	const QString entry = "scripts/test.cfg";
	const QByteArray payload = "sin fixture\n";
	const QString sin_path = QDir(root).filePath("fixture.sin");
	if (!write_pak_fixture(sin_path, "SPAK", 120, 128, entry, payload, error) ||
	    !expect_archive_entry(sin_path, Archive::Format::Pak, entry, payload, error)) {
		return false;
	}

	PakArchive sin;
	const QString rebuilt_path = QDir(root).filePath("fixture-copy.sin");
	if (!sin.load(sin_path, error)) {
		return false;
	}
	if (!sin.is_sin_archive()) {
		set_error(error, "SIN fixture did not load as SPAK.");
		return false;
	}
	if (!sin.save_as(rebuilt_path, error)) {
		return false;
	}
	return expect_archive_entry(rebuilt_path, Archive::Format::Pak, entry, payload, error);
}

bool test_zip_fixture(const QString& root, QString* error) {
	const QString source_path = QDir(root).filePath("zip-source/scripts/test.cfg");
	const QString entry = "scripts/test.cfg";
	const QByteArray payload = "zip fixture\n";
	if (!write_file(source_path, payload, error)) {
		return false;
	}

	ZipArchive::WritePlan plan;
	plan.disk_files.push_back(ZipArchive::DiskFile{entry, source_path, 100});
	const QString zip_path = QDir(root).filePath("fixture.pk3");
	if (!ZipArchive::write_rebuilt(zip_path, plan, false, error)) {
		return false;
	}
	return expect_archive_entry(zip_path, Archive::Format::Zip, entry, payload, error);
}

bool test_quakelive_pk3_fixture(const QString& root, QString* error) {
	const QString source_path = QDir(root).filePath("ql-source/scripts/test.cfg");
	const QString entry = "scripts/test.cfg";
	const QByteArray payload = "quakelive fixture\n";
	if (!write_file(source_path, payload, error)) {
		return false;
	}

	ZipArchive::WritePlan plan;
	plan.disk_files.push_back(ZipArchive::DiskFile{entry, source_path, 200});
	const QString pk3_path = QDir(root).filePath("quakelive.pk3");
	if (!ZipArchive::write_rebuilt(pk3_path, plan, true, error)) {
		return false;
	}
	return expect_archive_entry(pk3_path, Archive::Format::Zip, entry, payload, error, true);
}

bool test_resources_fixture(const QString& root, QString* error) {
	const QString entry = "generated/test.cfg";
	const QByteArray payload = "resources fixture\n";
	const QString path = QDir(root).filePath("fixture.resources");
	if (!write_resources_fixture(path, entry, payload, error)) {
		return false;
	}
	return expect_archive_entry(path, Archive::Format::Resources, entry, payload, error);
}

bool test_wad2_fixture(const QString& root, QString* error) {
	const QString payload_path = QDir(root).filePath("matrix.lmp");
	const QByteArray payload = "wad2 fixture\n";
	if (!write_file(payload_path, payload, error)) {
		return false;
	}

	WadArchive::WritePlan plan;
	plan.entries.push_back(WadArchive::WriteEntry{"matrix.lmp", payload_path, false});
	const QString path = QDir(root).filePath("fixture.wad2");
	if (!WadArchive::write_wad2(path, plan, error)) {
		return false;
	}
	return expect_archive_entry(path, Archive::Format::Wad, "matrix.lmp", payload, error);
}

bool test_wad3_fixture(const QString& root, QString* error) {
	const QByteArray payload = "wad3 fixture\n";
	const QString path = QDir(root).filePath("fixture.wad3");
	if (!write_q12_wad_fixture(path, "WAD3", "STONE", static_cast<quint8>('C'), payload, error)) {
		return false;
	}
	return expect_archive_entry(path, Archive::Format::Wad, "STONE.mip", payload, error, false, true);
}

bool test_doom_wad_fixture(const QString& root, QString* error) {
	const QByteArray payload(768, '\x2A');
	const QString path = QDir(root).filePath("fixture.wad");
	if (!write_doom_wad_fixture(path, "PLAYPAL", payload, error)) {
		return false;
	}
	return expect_archive_entry(path, Archive::Format::Wad, "PLAYPAL", payload, error, false, false, true);
}

bool test_image_fixture(const QString& root, QString* error) {
	const ImageDecodeResult decoded = decode_image_bytes(one_pixel_tga(), "fixture.tga");
	if (!decoded.ok()) {
		set_error(error, decoded.error.isEmpty() ? "TGA fixture decode failed." : decoded.error);
		return false;
	}
	if (decoded.image.size() != QSize(1, 1) || decoded.image.pixelColor(0, 0) != QColor(255, 0, 0)) {
		set_error(error, "TGA fixture decoded with unexpected dimensions or pixel color.");
		return false;
	}

	if (!supported_image_write_formats().contains("pcx")) {
		set_error(error, "PCX is missing from supported image write formats.");
		return false;
	}

	ImageWriteOptions options;
	options.format = "pcx";
	options.dither = false;
	const QString pcx_path = QDir(root).filePath("roundtrip.pcx");
	if (!write_image_file(decoded.image, pcx_path, options, error)) {
		return false;
	}

	const ImageDecodeResult roundtrip = decode_image_file(pcx_path);
	if (!roundtrip.ok()) {
		set_error(error, roundtrip.error.isEmpty() ? "PCX fixture decode failed." : roundtrip.error);
		return false;
	}
	if (roundtrip.image.size() != QSize(1, 1)) {
		set_error(error, "PCX fixture decoded with unexpected dimensions.");
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

	const QString root = temp.path();
	QString error;
	if (!test_directory_fixture(QDir(root).filePath("directory"), &error) ||
	    !test_pak_fixture(QDir(root).filePath("pak"), &error) ||
	    !test_sin_fixture(QDir(root).filePath("sin"), &error) ||
	    !test_zip_fixture(QDir(root).filePath("zip"), &error) ||
	    !test_quakelive_pk3_fixture(QDir(root).filePath("quakelive"), &error) ||
	    !test_resources_fixture(QDir(root).filePath("resources"), &error) ||
	    !test_wad2_fixture(QDir(root).filePath("wad2"), &error) ||
	    !test_wad3_fixture(QDir(root).filePath("wad3"), &error) ||
	    !test_doom_wad_fixture(QDir(root).filePath("doom-wad"), &error) ||
	    !test_image_fixture(QDir(root).filePath("images"), &error)) {
		fail_message(error.isEmpty() ? "Support matrix fixture test failed." : error);
		return 1;
	}

	return 0;
}
