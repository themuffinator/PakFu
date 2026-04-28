#include <algorithm>
#include <cstring>
#include <optional>

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
#include "formats/bsp_preview.h"
#include "formats/idtech_asset_loader.h"
#include "formats/idtech4_map.h"
#include "formats/image_loader.h"
#include "formats/image_writer.h"
#include "formats/model.h"
#include "formats/sprite_loader.h"
#include "pak/pak_archive.h"
#include "wad/wad_archive.h"
#include "zip/zip_archive.h"

namespace {
constexpr int kPakHeaderSize = 12;
constexpr int kWadHeaderSize = 12;
constexpr int kQ12WadDirEntrySize = 32;
constexpr int kDoomWadDirEntrySize = 16;
constexpr quint32 kHeretic2BspFlags = (1u << 24u) | (1u << 25u);

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

void append_i32_le(QByteArray* bytes, qint32 value) {
	append_u32_le(bytes, static_cast<quint32>(value));
}

void append_i16_le(QByteArray* bytes, qint16 value) {
	const auto v = static_cast<quint16>(value);
	bytes->append(static_cast<char>(v & 0xFFu));
	bytes->append(static_cast<char>((v >> 8u) & 0xFFu));
}

void append_f32_le(QByteArray* bytes, float value) {
	quint32 bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	append_u32_le(bytes, bits);
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

QByteArray one_pixel_m8() {
	constexpr int kM8MipLevels = 16;
	constexpr int kM8HeaderSize = 1040;
	constexpr int kNameOffset = 4;
	constexpr int kWidthOffset = 36;
	constexpr int kHeightOffset = 100;
	constexpr int kMipOffset = 164;
	constexpr int kPaletteOffset = 260;

	QByteArray bytes(kM8HeaderSize, '\0');
	write_u32_le(&bytes, 0, 2);
	const QByteArray name = "fixture";
	std::memcpy(bytes.data() + kNameOffset, name.constData(), static_cast<size_t>(name.size()));
	for (int i = 0; i < kM8MipLevels; ++i) {
		write_u32_le(&bytes, kWidthOffset + i * 4, 1);
		write_u32_le(&bytes, kHeightOffset + i * 4, 1);
		write_u32_le(&bytes, kMipOffset + i * 4, static_cast<quint32>(kM8HeaderSize + i));
	}
	for (int i = 0; i < 256; ++i) {
		bytes[kPaletteOffset + i * 3 + 0] = static_cast<char>(i);
		bytes[kPaletteOffset + i * 3 + 1] = static_cast<char>(i);
		bytes[kPaletteOffset + i * 3 + 2] = static_cast<char>(i);
	}
	bytes[kPaletteOffset + 42 * 3 + 0] = 12;
	bytes[kPaletteOffset + 42 * 3 + 1] = 34;
	bytes[kPaletteOffset + 42 * 3 + 2] = 56;
	for (int i = 0; i < kM8MipLevels; ++i) {
		bytes.append(static_cast<char>(42));
	}
	return bytes;
}

QByteArray one_pixel_m32() {
	constexpr int kM32MipLevels = 16;
	constexpr int kM32HeaderSize = 968;
	constexpr int kNameOffset = 4;
	constexpr int kWidthOffset = 516;
	constexpr int kHeightOffset = 580;
	constexpr int kMipOffset = 644;

	QByteArray bytes(kM32HeaderSize, '\0');
	write_u32_le(&bytes, 0, 4);
	const QByteArray name = "fixture";
	std::memcpy(bytes.data() + kNameOffset, name.constData(), static_cast<size_t>(name.size()));
	for (int i = 0; i < kM32MipLevels; ++i) {
		write_u32_le(&bytes, kWidthOffset + i * 4, 1);
		write_u32_le(&bytes, kHeightOffset + i * 4, 1);
		write_u32_le(&bytes, kMipOffset + i * 4, static_cast<quint32>(kM32HeaderSize + i * 4));
	}
	for (int i = 0; i < kM32MipLevels; ++i) {
		bytes.append(static_cast<char>(12));
		bytes.append(static_cast<char>(34));
		bytes.append(static_cast<char>(56));
		bytes.append(static_cast<char>(255));
	}
	return bytes;
}

QByteArray one_tile_bk(QString* error) {
	QByteArray bytes;
	append_u32_le(&bytes, 0x4B4F4F42u);  // BOOK
	append_i32_le(&bytes, 2);
	append_i32_le(&bytes, 1);
	append_i32_le(&bytes, 2);
	append_i32_le(&bytes, 2);
	append_i32_le(&bytes, 1);
	append_i32_le(&bytes, 0);
	append_i32_le(&bytes, 1);
	append_i32_le(&bytes, 1);
	if (!append_fixed_name(&bytes, "tile.m8", 64, error)) {
		return {};
	}
	return bytes;
}

QByteArray minimal_os_script() {
	QByteArray bytes;
	append_i32_le(&bytes, 3);
	bytes.append(static_cast<char>(20));  // CODE_EXIT
	return bytes;
}

bool append_fm_block(QByteArray* bytes, const QString& name, qint32 version, const QByteArray& payload, QString* error) {
	if (!append_fixed_name(bytes, name, 32, error)) {
		return false;
	}
	append_i32_le(bytes, version);
	append_i32_le(bytes, static_cast<qint32>(payload.size()));
	bytes->append(payload);
	return true;
}

QByteArray one_triangle_fm(QString* error, const QString& skin_name = QStringLiteral("triangle.m8")) {
	constexpr qint32 skin_width = 64;
	constexpr qint32 skin_height = 64;
	constexpr qint32 vertex_count = 3;
	constexpr qint32 st_count = 3;
	constexpr qint32 triangle_count = 1;
	constexpr qint32 frame_count = 1;
	constexpr qint32 mesh_node_count = 1;
	constexpr qint32 frame_size = 40 + vertex_count * 4;

	QByteArray header;
	append_i32_le(&header, skin_width);
	append_i32_le(&header, skin_height);
	append_i32_le(&header, frame_size);
	append_i32_le(&header, 1);
	append_i32_le(&header, vertex_count);
	append_i32_le(&header, st_count);
	append_i32_le(&header, triangle_count);
	append_i32_le(&header, 0);
	append_i32_le(&header, frame_count);
	append_i32_le(&header, mesh_node_count);

	QByteArray skins;
	if (!append_fixed_name(&skins, skin_name, 64, error)) {
		return {};
	}

	QByteArray st;
	append_i16_le(&st, 0);
	append_i16_le(&st, 0);
	append_i16_le(&st, skin_width);
	append_i16_le(&st, 0);
	append_i16_le(&st, 0);
	append_i16_le(&st, skin_height);

	QByteArray tris;
	append_i16_le(&tris, 0);
	append_i16_le(&tris, 1);
	append_i16_le(&tris, 2);
	append_i16_le(&tris, 0);
	append_i16_le(&tris, 1);
	append_i16_le(&tris, 2);

	QByteArray frames;
	append_f32_le(&frames, 1.0f);
	append_f32_le(&frames, 1.0f);
	append_f32_le(&frames, 1.0f);
	append_f32_le(&frames, 0.0f);
	append_f32_le(&frames, 0.0f);
	append_f32_le(&frames, 0.0f);
	if (!append_fixed_name(&frames, "frame0", 16, error)) {
		return {};
	}
	frames.append('\0');
	frames.append('\0');
	frames.append('\0');
	frames.append('\0');
	frames.append(static_cast<char>(16));
	frames.append('\0');
	frames.append('\0');
	frames.append('\0');
	frames.append('\0');
	frames.append(static_cast<char>(16));
	frames.append('\0');
	frames.append('\0');

	QByteArray mesh_node(516, '\0');
	mesh_node[0] = 0x01;

	QByteArray fm;
	if (!append_fm_block(&fm, "header", 2, header, error) ||
	    !append_fm_block(&fm, "skin", 1, skins, error) ||
	    !append_fm_block(&fm, "st coord", 1, st, error) ||
	    !append_fm_block(&fm, "tris", 1, tris, error) ||
	    !append_fm_block(&fm, "frames", 1, frames, error) ||
	    !append_fm_block(&fm, "mesh nodes", 3, mesh_node, error)) {
		return {};
	}
	return fm;
}

QByteArray one_triangle_heretic2_bsp(bool converted_qbsp, const QString& texture_name, QString* error) {
	constexpr int kQ2LumpCount = 19;
	constexpr int kQ2HeaderSize = 8 + kQ2LumpCount * 8;
	constexpr int kQ2Vertices = 2;
	constexpr int kQ2TexInfo = 5;
	constexpr int kQ2Faces = 6;
	constexpr int kQ2Edges = 11;
	constexpr int kQ2Surfedges = 12;

	const QByteArray tex_latin1 = texture_name.toLatin1();
	if (QString::fromLatin1(tex_latin1) != texture_name) {
		set_error(error, "Heretic II BSP fixture texture name must be Latin-1.");
		return {};
	}
	const int texture_bytes = converted_qbsp ? 64 : 32;
	if (tex_latin1.size() > texture_bytes) {
		set_error(error, QString("Heretic II BSP fixture texture name is too long: %1").arg(texture_name));
		return {};
	}

	QByteArray bytes(kQ2HeaderSize, '\0');
	std::memcpy(bytes.data(), converted_qbsp ? "QBSP" : "IBSP", 4);
	write_u32_le(&bytes, 4, 38);

	auto add_lump = [&](int lump_index, const QByteArray& payload) {
		while ((bytes.size() % 4) != 0) {
			bytes.append('\0');
		}
		const int offset = bytes.size();
		bytes.append(payload);
		write_u32_le(&bytes, 8 + lump_index * 8 + 0, static_cast<quint32>(offset));
		write_u32_le(&bytes, 8 + lump_index * 8 + 4, static_cast<quint32>(payload.size()));
	};

	QByteArray vertices;
	append_f32_le(&vertices, 0.0f);
	append_f32_le(&vertices, 0.0f);
	append_f32_le(&vertices, 0.0f);
	append_f32_le(&vertices, 64.0f);
	append_f32_le(&vertices, 0.0f);
	append_f32_le(&vertices, 0.0f);
	append_f32_le(&vertices, 0.0f);
	append_f32_le(&vertices, 64.0f);
	append_f32_le(&vertices, 0.0f);
	add_lump(kQ2Vertices, vertices);

	QByteArray texinfo;
	append_f32_le(&texinfo, 1.0f);
	append_f32_le(&texinfo, 0.0f);
	append_f32_le(&texinfo, 0.0f);
	append_f32_le(&texinfo, 0.0f);
	append_f32_le(&texinfo, 0.0f);
	append_f32_le(&texinfo, 1.0f);
	append_f32_le(&texinfo, 0.0f);
	append_f32_le(&texinfo, 0.0f);
	append_u32_le(&texinfo, kHeretic2BspFlags);
	append_i32_le(&texinfo, 0);
	if (converted_qbsp) {
		if (!append_fixed_name(&texinfo, "stone", 16, error)) {
			return {};
		}
		if (!append_fixed_name(&texinfo, texture_name, 64, error)) {
			return {};
		}
	} else if (!append_fixed_name(&texinfo, texture_name, 32, error)) {
		return {};
	}
	append_i32_le(&texinfo, -1);
	add_lump(kQ2TexInfo, texinfo);

	QByteArray edges;
	if (converted_qbsp) {
		append_u32_le(&edges, 0);
		append_u32_le(&edges, 1);
		append_u32_le(&edges, 1);
		append_u32_le(&edges, 2);
		append_u32_le(&edges, 2);
		append_u32_le(&edges, 0);
	} else {
		append_i16_le(&edges, 0);
		append_i16_le(&edges, 1);
		append_i16_le(&edges, 1);
		append_i16_le(&edges, 2);
		append_i16_le(&edges, 2);
		append_i16_le(&edges, 0);
	}
	add_lump(kQ2Edges, edges);

	QByteArray surfedges;
	append_i32_le(&surfedges, 0);
	append_i32_le(&surfedges, 1);
	append_i32_le(&surfedges, 2);
	add_lump(kQ2Surfedges, surfedges);

	QByteArray faces;
	if (converted_qbsp) {
		append_u32_le(&faces, 0);
		append_u32_le(&faces, 0);
		append_u32_le(&faces, 0);
		append_u32_le(&faces, 3);
		append_i32_le(&faces, 0);
		faces.append('\0');
		faces.append(static_cast<char>(0xFF));
		faces.append(static_cast<char>(0xFF));
		faces.append(static_cast<char>(0xFF));
		append_i32_le(&faces, -1);
	} else {
		append_i16_le(&faces, 0);
		append_i16_le(&faces, 0);
		append_i32_le(&faces, 0);
		append_i16_le(&faces, 3);
		append_i16_le(&faces, 0);
		faces.append('\0');
		faces.append(static_cast<char>(0xFF));
		faces.append(static_cast<char>(0xFF));
		faces.append(static_cast<char>(0xFF));
		append_i32_le(&faces, -1);
	}
	add_lump(kQ2Faces, faces);

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

	ImageDecodeOptions m8_options;
	m8_options.mip_level = 15;
	const ImageDecodeResult m8 = decode_image_bytes(one_pixel_m8(), "fixture.m8", m8_options);
	if (!m8.ok()) {
		set_error(error, m8.error.isEmpty() ? "M8 fixture decode failed." : m8.error);
		return false;
	}
	if (m8.image.size() != QSize(1, 1) || m8.image.pixelColor(0, 0) != QColor(12, 34, 56, 255)) {
		set_error(error, "M8 fixture decoded with unexpected dimensions or pixel color.");
		return false;
	}

	ImageDecodeOptions m32_options;
	m32_options.mip_level = 15;
	const ImageDecodeResult m32 = decode_image_bytes(one_pixel_m32(), "fixture.m32", m32_options);
	if (!m32.ok()) {
		set_error(error, m32.error.isEmpty() ? "M32 fixture decode failed." : m32.error);
		return false;
	}
	if (m32.image.size() != QSize(1, 1) || m32.image.pixelColor(0, 0) != QColor(12, 34, 56, 255)) {
		set_error(error, "M32 fixture decoded with unexpected dimensions or pixel color.");
		return false;
	}
	return true;
}

bool test_heretic2_asset_fixture(const QString& root, QString* error) {
	Q_UNUSED(root);

	if (!is_supported_idtech_asset_file("book/page.bk") || !is_supported_idtech_asset_file("ds/intro.os")) {
		set_error(error, "Heretic II BK/OS asset extensions are missing from the idTech asset registry.");
		return false;
	}

	const QByteArray bk = one_tile_bk(error);
	if (bk.isEmpty()) {
		return false;
	}

	const BkTileLoader tile_loader = [](const QString& tile_name) -> ImageDecodeResult {
		QString normalized = tile_name.toLower();
		normalized.replace('\\', '/');
		if (normalized == "book/tile.m8" || normalized == "tile.m8") {
			return decode_image_bytes(one_pixel_m8(), "tile.m8");
		}
		return ImageDecodeResult{QImage(), QString("Unexpected BK tile request: %1").arg(tile_name)};
	};

	const SpriteDecodeResult sprite = decode_bk_sprite(bk, tile_loader);
	if (!sprite.ok() || sprite.frames.size() != 1 || sprite.frames[0].image.size() != QSize(2, 2)) {
		set_error(error, sprite.error.isEmpty() ? "BK fixture decode failed." : sprite.error);
		return false;
	}
	if (sprite.frames[0].image.pixelColor(1, 0) != QColor(12, 34, 56, 255) ||
	    sprite.frames[0].image.pixelColor(0, 0).alpha() != 0) {
		set_error(error, "BK fixture composited with unexpected pixels.");
		return false;
	}

	const IdTechAssetDecodeResult bk_meta = decode_idtech_asset_bytes(bk, "fixture.bk");
	if (!bk_meta.ok() || !bk_meta.summary.contains("Heretic II BOOK sprite") || !bk_meta.summary.contains("0lvin/heretic2")) {
		set_error(error, bk_meta.error.isEmpty() ? "BK metadata decode failed." : bk_meta.error);
		return false;
	}

	const QByteArray os = minimal_os_script();
	const IdTechAssetDecodeResult os_meta = decode_idtech_asset_bytes(os, "intro.os");
	if (!os_meta.ok() || !os_meta.summary.contains("Version: 3") || !os_meta.summary.contains("CODE_EXIT") ||
	    !os_meta.summary.contains("ds/<script>.os")) {
		set_error(error, os_meta.error.isEmpty() ? "OS metadata decode failed." : os_meta.error);
		return false;
	}

	return true;
}

bool test_model_fixture(const QString& root, QString* error) {
	const QString fm_path = QDir(root).filePath("triangle.fm");
	const QByteArray fm = one_triangle_fm(error);
	if (fm.isEmpty()) {
		return false;
	}
	if (!write_file(fm_path, fm, error)) {
		return false;
	}

	const std::optional<LoadedModel> loaded = load_model_file(fm_path, error);
	if (!loaded) {
		return false;
	}
	if (loaded->format != "fm" || loaded->mesh.vertices.size() != 3 || loaded->mesh.indices.size() != 3 ||
	    loaded->surfaces.size() != 1 || loaded->surfaces[0].shader != "triangle.m8") {
		set_error(error, "FM fixture loaded with unexpected geometry or skin metadata.");
		return false;
	}

	const QString fm_m32_path = QDir(root).filePath("triangle_m32.fm");
	const QByteArray fm_m32 = one_triangle_fm(error, QStringLiteral("triangle.m32"));
	if (fm_m32.isEmpty()) {
		return false;
	}
	if (!write_file(fm_m32_path, fm_m32, error)) {
		return false;
	}

	const std::optional<LoadedModel> loaded_m32 = load_model_file(fm_m32_path, error);
	if (!loaded_m32) {
		return false;
	}
	if (loaded_m32->format != "fm" || loaded_m32->surfaces.size() != 1 ||
	    loaded_m32->surfaces[0].shader != "triangle.m32") {
		set_error(error, "FM fixture did not preserve Heretic II M32 skin metadata.");
		return false;
	}
	return true;
}

bool test_bsp_fixture(const QString& root, QString* error) {
	struct Case {
		bool converted_qbsp = false;
		QString name;
		QString texture;
	};
	const QVector<Case> cases = {
		{false, "heretic2-native.bsp", "textures/h2/floor"},
		{true, "heretic2-converted.bsp", "textures/heretic2/long_material_floor_0123456789"},
	};

	for (const Case& c : cases) {
		const QByteArray bsp = one_triangle_heretic2_bsp(c.converted_qbsp, c.texture, error);
		if (bsp.isEmpty()) {
			return false;
		}
		const QString path = QDir(root).filePath(c.name);
		if (!write_file(path, bsp, error)) {
			return false;
		}
		if (bsp_family_bytes(bsp, error) != BspFamily::Heretic2) {
			set_error(error, QString("Heretic II BSP fixture was not classified correctly: %1").arg(c.name));
			return false;
		}
		BspMesh mesh;
		if (!load_bsp_mesh_bytes(bsp, c.name, &mesh, error, false)) {
			return false;
		}
		if (mesh.indices.size() != 3 || mesh.surfaces.size() != 1 || mesh.surfaces[0].texture != c.texture) {
			set_error(error, QString("Heretic II BSP fixture loaded with unexpected geometry or texture: %1").arg(c.name));
			return false;
		}
	}
	return true;
}

bool test_idtech4_map_fixture(const QString& root, QString* error) {
	const QByteArray map = QByteArrayLiteral(
	  "Version 2\n"
	  "{\n"
	  "\"classname\" \"worldspawn\"\n"
	  "{\n"
	  "brushDef3\n"
	  "{\n"
	  "( 0 0 1 -64 ) ( ( 0.03125 0 0 ) ( 0 0.03125 0 ) ) \"textures/base_wall/stone\"\n"
	  "}\n"
	  "}\n"
	  "}\n"
	  "{\n"
	  "\"classname\" \"light\"\n"
	  "\"origin\" \"0 0 64\"\n"
	  "}\n");
	const QString map_path = QDir(root).filePath("test.map");
	if (!write_file(map_path, map, error)) {
		return false;
	}
	const IdTech4MapInspectResult map_summary = inspect_idtech4_map_bytes(map, "maps/test.map");
	if (!map_summary.ok() || map_summary.artifact != IdTech4MapArtifact::SourceMap ||
	    !map_summary.summary.contains("Detected idTech4 hints: yes") ||
	    !map_summary.summary.contains("Top-level map blocks: 2") ||
	    !map_summary.summary.contains("brushDef3 blocks: 1") ||
	    !map_summary.summary.contains("worldspawn: 1")) {
		set_error(error, map_summary.error.isEmpty() ? "idTech4 .map fixture summary was unexpected." : map_summary.error);
		return false;
	}

	const QByteArray proc = QByteArrayLiteral(
	  "PROC \"4\"\n"
	  "model {\n"
	  "\"maps/test/world\" 1\n"
	  "{ \"textures/base_wall/stone\" 3 3 0\n"
	  "( 0 0 0 0 0 0 0 1 1 0 0 )\n"
	  "( 64 0 0 1 0 0 0 1 1 0 0 )\n"
	  "( 0 64 0 0 1 0 0 1 1 0 0 )\n"
	  "0 1 2\n"
	  "}\n"
	  "}\n"
	  "interAreaPortals { 1 0 }\n"
	  "nodes { 0 }\n");
	const QString proc_path = QDir(root).filePath("test.proc");
	if (!write_file(proc_path, proc, error)) {
		return false;
	}
	const IdTech4MapInspectResult proc_summary = inspect_idtech4_map_bytes(proc, "maps/test.proc");
	if (!proc_summary.ok() || proc_summary.artifact != IdTech4MapArtifact::ProcFile ||
	    !proc_summary.summary.contains("PROC version: 4") ||
	    !proc_summary.summary.contains("Model sections: 1") ||
	    !proc_summary.summary.contains("Inter-area portal sections: 1") ||
	    !proc_summary.summary.contains("Node sections: 1")) {
		set_error(error, proc_summary.error.isEmpty() ? "idTech4 .proc fixture summary was unexpected." : proc_summary.error);
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
	    !test_image_fixture(QDir(root).filePath("images"), &error) ||
	    !test_heretic2_asset_fixture(QDir(root).filePath("heretic2-assets"), &error) ||
	    !test_model_fixture(QDir(root).filePath("models"), &error) ||
	    !test_bsp_fixture(QDir(root).filePath("bsps"), &error) ||
	    !test_idtech4_map_fixture(QDir(root).filePath("idtech4-maps"), &error)) {
		fail_message(error.isEmpty() ? "Support matrix fixture test failed." : error);
		return 1;
	}

	return 0;
}
